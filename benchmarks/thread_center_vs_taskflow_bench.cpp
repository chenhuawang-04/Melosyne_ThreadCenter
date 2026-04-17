#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <taskflow/algorithm/for_each.hpp>
#include <taskflow/algorithm/pipeline.hpp>
#include <taskflow/taskflow.hpp>

#if defined(THREAD_CENTER_USE_MODULES)
import Center.Thread;
#else
#include "thread_center/thread_center.hpp"
#endif

namespace
{

using Clock = std::chrono::steady_clock;

struct BenchConfig final
{
  std::uint32_t workers{4};
  std::uint32_t warmup_iterations{16};
  std::uint32_t measure_iterations{64};
  std::uint32_t repeats{3};
  std::vector<std::string> scenario_filters{};
  std::optional<std::filesystem::path> csv_output_path{};
  std::optional<std::filesystem::path> json_output_path{};
};

struct Stats final
{
  double mean_us{0.0};
  double median_us{0.0};
  double p95_us{0.0};
  double min_us{0.0};
  double max_us{0.0};
  double stddev_us{0.0};
};

struct ScenarioSamples final
{
  std::string name{};
  std::vector<double> thread_center_samples_us{};
  std::vector<double> taskflow_samples_us{};
};

struct ScenarioResult final
{
  std::string name{};
  Stats thread_center{};
  Stats taskflow{};
  std::size_t sample_count{0};
};

[[nodiscard]] auto toMicroseconds(Clock::duration duration_) -> double
{
  return std::chrono::duration<double, std::micro>(duration_).count();
}

[[nodiscard]] auto computeStats(std::vector<double> samples_us_) -> Stats
{
  Stats stats;
  if (samples_us_.empty()) {
    return stats;
  }

  std::sort(samples_us_.begin(), samples_us_.end());
  const auto sample_count = samples_us_.size();
  const auto sum_value = std::accumulate(samples_us_.begin(), samples_us_.end(), 0.0);

  stats.mean_us = sum_value / static_cast<double>(sample_count);
  stats.min_us = samples_us_.front();
  stats.max_us = samples_us_.back();
  stats.median_us = sample_count % 2 == 0
                      ? (samples_us_[sample_count / 2 - 1] + samples_us_[sample_count / 2]) * 0.5
                      : samples_us_[sample_count / 2];
  stats.p95_us = samples_us_[std::min(sample_count - 1, (sample_count * 95) / 100)];

  double variance = 0.0;
  for (const auto sample_value : samples_us_) {
    const auto delta = sample_value - stats.mean_us;
    variance += delta * delta;
  }
  variance /= static_cast<double>(sample_count);
  stats.stddev_us = std::sqrt(variance);

  return stats;
}

[[nodiscard]] auto shouldRunScenario(const BenchConfig& config_, std::string_view scenario_name_)
  -> bool
{
  if (config_.scenario_filters.empty()) {
    return true;
  }

  for (const auto& filter : config_.scenario_filters) {
    if (scenario_name_.find(filter) != std::string_view::npos) {
      return true;
    }
  }

  return false;
}

template <class TaskflowFn, class ThreadCenterFn>
[[nodiscard]] auto collectScenarioSamples(std::string name_,
                                          const BenchConfig& config_,
                                          TaskflowFn&& taskflow_fn_,
                                          ThreadCenterFn&& thread_center_fn_) -> ScenarioSamples
{
  auto scenario_samples = ScenarioSamples{.name = std::move(name_)};
  scenario_samples.taskflow_samples_us.reserve(
    static_cast<std::size_t>(config_.measure_iterations) * config_.repeats);
  scenario_samples.thread_center_samples_us.reserve(
    static_cast<std::size_t>(config_.measure_iterations) * config_.repeats);

  auto taskflow_measure = [&] {
    const auto begin_time = Clock::now();
    taskflow_fn_();
    const auto end_time = Clock::now();
    return toMicroseconds(end_time - begin_time);
  };

  auto thread_center_measure = [&] {
    const auto begin_time = Clock::now();
    thread_center_fn_();
    const auto end_time = Clock::now();
    return toMicroseconds(end_time - begin_time);
  };

  for (std::uint32_t repeat_index = 0; repeat_index < config_.repeats; ++repeat_index) {
    for (std::uint32_t warmup_index = 0; warmup_index < config_.warmup_iterations; ++warmup_index) {
      if ((warmup_index & 1u) == 0u) {
        static_cast<void>(taskflow_measure());
        static_cast<void>(thread_center_measure());
      }
      else {
        static_cast<void>(thread_center_measure());
        static_cast<void>(taskflow_measure());
      }
    }

    for (std::uint32_t measure_index = 0; measure_index < config_.measure_iterations;
         ++measure_index) {
      if ((measure_index & 1u) == 0u) {
        scenario_samples.taskflow_samples_us.push_back(taskflow_measure());
        scenario_samples.thread_center_samples_us.push_back(thread_center_measure());
      }
      else {
        scenario_samples.thread_center_samples_us.push_back(thread_center_measure());
        scenario_samples.taskflow_samples_us.push_back(taskflow_measure());
      }
    }
  }

  return scenario_samples;
}

[[nodiscard]] auto summarizeScenario(const ScenarioSamples& scenario_samples_) -> ScenarioResult
{
  return ScenarioResult{
    .name = scenario_samples_.name,
    .thread_center = computeStats(scenario_samples_.thread_center_samples_us),
    .taskflow = computeStats(scenario_samples_.taskflow_samples_us),
    .sample_count = scenario_samples_.thread_center_samples_us.size(),
  };
}

void printScenarioResult(const ScenarioResult& result_)
{
  const auto ratio = result_.taskflow.mean_us == 0.0
                       ? 0.0
                       : result_.thread_center.mean_us / result_.taskflow.mean_us;

  std::cout << "\n[Scenario] " << result_.name << '\n';
  std::cout << "  Taskflow      mean=" << std::setw(10) << std::fixed << std::setprecision(2)
            << result_.taskflow.mean_us << "us"
            << "  median=" << std::setw(10) << result_.taskflow.median_us << "us"
            << "  p95=" << std::setw(10) << result_.taskflow.p95_us << "us"
            << "  min=" << std::setw(10) << result_.taskflow.min_us << "us"
            << "  max=" << std::setw(10) << result_.taskflow.max_us << "us"
            << "  stddev=" << std::setw(10) << result_.taskflow.stddev_us << "us" << '\n';
  std::cout << "  ThreadCenter  mean=" << std::setw(10) << result_.thread_center.mean_us << "us"
            << "  median=" << std::setw(10) << result_.thread_center.median_us << "us"
            << "  p95=" << std::setw(10) << result_.thread_center.p95_us << "us"
            << "  min=" << std::setw(10) << result_.thread_center.min_us << "us"
            << "  max=" << std::setw(10) << result_.thread_center.max_us << "us"
            << "  stddev=" << std::setw(10) << result_.thread_center.stddev_us << "us" << '\n';
  std::cout << "  Ratio(ThreadCenter/Taskflow mean) = " << std::setprecision(4) << ratio
            << "  samples=" << result_.sample_count << '\n';
}

void writeCsvReport(const std::filesystem::path& path_,
                    const BenchConfig& config_,
                    const std::vector<ScenarioResult>& results_)
{
  auto output = std::ofstream{path_, std::ios::binary};
  output << "scenario,impl,mean_us,median_us,p95_us,min_us,max_us,stddev_us,samples,workers,warmup,"
            "measure,repeats\n";

  for (const auto& result : results_) {
    output << '"' << result.name << "\",taskflow," << result.taskflow.mean_us << ','
           << result.taskflow.median_us << ',' << result.taskflow.p95_us << ','
           << result.taskflow.min_us << ',' << result.taskflow.max_us << ','
           << result.taskflow.stddev_us << ',' << result.sample_count << ',' << config_.workers
           << ',' << config_.warmup_iterations << ',' << config_.measure_iterations << ','
           << config_.repeats << "\n";

    output << '"' << result.name << "\",thread_center," << result.thread_center.mean_us << ','
           << result.thread_center.median_us << ',' << result.thread_center.p95_us << ','
           << result.thread_center.min_us << ',' << result.thread_center.max_us << ','
           << result.thread_center.stddev_us << ',' << result.sample_count << ',' << config_.workers
           << ',' << config_.warmup_iterations << ',' << config_.measure_iterations << ','
           << config_.repeats << "\n";
  }
}

void writeJsonReport(const std::filesystem::path& path_,
                     const BenchConfig& config_,
                     const std::vector<ScenarioResult>& results_)
{
  auto output = std::ofstream{path_, std::ios::binary};
  output << "{\n";
  output << "  \"config\": {\n";
  output << "    \"workers\": " << config_.workers << ",\n";
  output << "    \"warmup_iterations\": " << config_.warmup_iterations << ",\n";
  output << "    \"measure_iterations\": " << config_.measure_iterations << ",\n";
  output << "    \"repeats\": " << config_.repeats << "\n";
  output << "  },\n";
  output << "  \"results\": [\n";

  for (std::size_t index = 0; index < results_.size(); ++index) {
    const auto& result = results_[index];
    const auto ratio =
      result.taskflow.mean_us == 0.0 ? 0.0 : result.thread_center.mean_us / result.taskflow.mean_us;

    output << "    {\n";
    output << "      \"scenario\": \"" << result.name << "\",\n";
    output << "      \"samples\": " << result.sample_count << ",\n";
    output << "      \"ratio_thread_center_over_taskflow\": " << ratio << ",\n";
    output << "      \"taskflow\": {\n";
    output << "        \"mean_us\": " << result.taskflow.mean_us << ",\n";
    output << "        \"median_us\": " << result.taskflow.median_us << ",\n";
    output << "        \"p95_us\": " << result.taskflow.p95_us << ",\n";
    output << "        \"min_us\": " << result.taskflow.min_us << ",\n";
    output << "        \"max_us\": " << result.taskflow.max_us << ",\n";
    output << "        \"stddev_us\": " << result.taskflow.stddev_us << "\n";
    output << "      },\n";
    output << "      \"thread_center\": {\n";
    output << "        \"mean_us\": " << result.thread_center.mean_us << ",\n";
    output << "        \"median_us\": " << result.thread_center.median_us << ",\n";
    output << "        \"p95_us\": " << result.thread_center.p95_us << ",\n";
    output << "        \"min_us\": " << result.thread_center.min_us << ",\n";
    output << "        \"max_us\": " << result.thread_center.max_us << ",\n";
    output << "        \"stddev_us\": " << result.thread_center.stddev_us << "\n";
    output << "      }\n";
    output << "    }" << (index + 1 == results_.size() ? "\n" : ",\n");
  }

  output << "  ]\n";
  output << "}\n";
}

void printUsage(std::string_view program_name_)
{
  std::cout << "Usage: " << program_name_ << " [options]\n"
            << "  --workers N        worker count\n"
            << "  --warmup N         warmup iterations per repeat\n"
            << "  --measure N        measured iterations per repeat\n"
            << "  --repeats N        repeat groups\n"
            << "  --scenario NAME    run only scenarios containing NAME (repeatable)\n"
            << "  --csv PATH         write csv summary\n"
            << "  --json PATH        write json summary\n"
            << "  --list-scenarios   list all available scenario names\n"
            << "  --help             show this message\n";
}

[[nodiscard]] auto parseUIntArg(const std::vector<std::string>& args_,
                                std::size_t& index_,
                                std::uint32_t& value_) -> bool
{
  if (index_ + 1 >= args_.size()) {
    return false;
  }

  value_ = static_cast<std::uint32_t>(std::stoul(args_[++index_]));
  return true;
}

[[nodiscard]] auto
parseArgs(const std::vector<std::string>& args_, BenchConfig& config_, bool& list_only_) -> bool
{
  for (std::size_t index = 1; index < args_.size(); ++index) {
    const auto& arg = args_[index];
    if (arg == "--workers") {
      if (!parseUIntArg(args_, index, config_.workers)) {
        return false;
      }
    }
    else if (arg == "--warmup") {
      if (!parseUIntArg(args_, index, config_.warmup_iterations)) {
        return false;
      }
    }
    else if (arg == "--measure") {
      if (!parseUIntArg(args_, index, config_.measure_iterations)) {
        return false;
      }
    }
    else if (arg == "--repeats") {
      if (!parseUIntArg(args_, index, config_.repeats)) {
        return false;
      }
    }
    else if (arg == "--scenario") {
      if (index + 1 >= args_.size()) {
        return false;
      }
      config_.scenario_filters.push_back(args_[++index]);
    }
    else if (arg == "--csv") {
      if (index + 1 >= args_.size()) {
        return false;
      }
      config_.csv_output_path = std::filesystem::path{args_[++index]};
    }
    else if (arg == "--json") {
      if (index + 1 >= args_.size()) {
        return false;
      }
      config_.json_output_path = std::filesystem::path{args_[++index]};
    }
    else if (arg == "--list-scenarios") {
      list_only_ = true;
    }
    else if (arg == "--help") {
      return false;
    }
    else {
      std::cerr << "Unknown argument: " << arg << '\n';
      return false;
    }
  }

  config_.workers = std::max(config_.workers, 1u);
  config_.warmup_iterations = std::max(config_.warmup_iterations, 1u);
  config_.measure_iterations = std::max(config_.measure_iterations, 1u);
  config_.repeats = std::max(config_.repeats, 1u);

  return true;
}

[[nodiscard]] auto runChainBuildRun(const BenchConfig& config_) -> ScenarioSamples
{
  constexpr std::size_t kChainLength = 256;

  auto taskflow_executor = tf::Executor{config_.workers};
  auto thread_center =
    ThreadCenter::Center{ThreadCenter::ExecutorConfig{.workers = config_.workers}};

  auto taskflow_runner = [&] {
    tf::Taskflow taskflow;
    std::atomic_int sink{0};

    auto previous_task = taskflow.emplace([&] {
      sink.fetch_add(1, std::memory_order_relaxed);
    });
    for (std::size_t index = 1; index < kChainLength; ++index) {
      auto current_task = taskflow.emplace([&] {
        sink.fetch_add(1, std::memory_order_relaxed);
      });
      previous_task.precede(current_task);
      previous_task = current_task;
    }

    taskflow_executor.run(taskflow).wait();
  };

  auto thread_center_runner = [&] {
    auto plan = thread_center.makePlan();
    std::atomic_int sink{0};

    auto previous_task = plan.task({.name = "chain_0"}, [&] {
      sink.fetch_add(1, std::memory_order_relaxed);
    });
    for (std::size_t index = 1; index < kChainLength; ++index) {
      auto current_task = plan.task({.name = "chain_task"}, [&] {
        sink.fetch_add(1, std::memory_order_relaxed);
      });
      plan.precede(previous_task, current_task);
      previous_task = current_task;
    }

    auto run_handle = thread_center.dispatch(plan);
    run_handle.wait();
  };

  return collectScenarioSamples(
    "ChainBuildRun(256 tasks)", config_, taskflow_runner, thread_center_runner);
}

[[nodiscard]] auto runFanOutFanInBuildRun(const BenchConfig& config_) -> ScenarioSamples
{
  constexpr std::size_t kLeafCount = 256;

  auto taskflow_executor = tf::Executor{config_.workers};
  auto thread_center =
    ThreadCenter::Center{ThreadCenter::ExecutorConfig{.workers = config_.workers}};

  auto taskflow_runner = [&] {
    tf::Taskflow taskflow;
    std::atomic_int sink{0};

    auto begin_task = taskflow.emplace([] {});
    auto end_task = taskflow.emplace([&] {
      sink.fetch_add(1, std::memory_order_relaxed);
    });

    for (std::size_t leaf = 0; leaf < kLeafCount; ++leaf) {
      auto leaf_task = taskflow.emplace([&] {
        sink.fetch_add(1, std::memory_order_relaxed);
      });
      begin_task.precede(leaf_task);
      leaf_task.precede(end_task);
    }

    taskflow_executor.run(taskflow).wait();
  };

  auto thread_center_runner = [&] {
    auto plan = thread_center.makePlan();
    std::atomic_int sink{0};

    auto begin_task = plan.task({.name = "begin"}, [] {});
    auto end_task = plan.task({.name = "end"}, [&] {
      sink.fetch_add(1, std::memory_order_relaxed);
    });

    for (std::size_t leaf = 0; leaf < kLeafCount; ++leaf) {
      auto leaf_task = plan.task({.name = "leaf"}, [&] {
        sink.fetch_add(1, std::memory_order_relaxed);
      });
      plan.precede(begin_task, leaf_task);
      plan.precede(leaf_task, end_task);
    }

    auto run_handle = thread_center.dispatch(plan);
    run_handle.wait();
  };

  return collectScenarioSamples(
    "FanOutFanInBuildRun(256 leaves)", config_, taskflow_runner, thread_center_runner);
}

[[nodiscard]] auto runParallelForBuildRun(const BenchConfig& config_) -> ScenarioSamples
{
  constexpr std::size_t kItemCount = 1 << 15;

  auto taskflow_executor = tf::Executor{config_.workers};
  auto thread_center =
    ThreadCenter::Center{ThreadCenter::ExecutorConfig{.workers = config_.workers}};
  auto values = std::vector<std::int32_t>(kItemCount, 1);

  auto taskflow_runner = [&] {
    tf::Taskflow taskflow;
    std::atomic<std::int64_t> sum{0};

    taskflow.for_each_index(std::size_t{0}, values.size(), std::size_t{1}, [&](std::size_t index_) {
      sum.fetch_add(values[index_], std::memory_order_relaxed);
    });
    taskflow_executor.run(taskflow).wait();
  };

  auto thread_center_runner = [&] {
    auto plan = thread_center.makePlan();
    std::atomic<std::int64_t> sum{0};

    static_cast<void>(plan.parallelFor({.name = "parallel_for",
                                        .task_kind = ThreadCenter::TaskKind::PARALLEL_FOR,
                                        .min_grain_size = 128},
                                       std::size_t{0},
                                       values.size(),
                                       std::size_t{1},
                                       [&](std::size_t index_) {
                                         sum.fetch_add(values[index_], std::memory_order_relaxed);
                                       }));

    auto run_handle = thread_center.dispatch(plan);
    run_handle.wait();
  };

  return collectScenarioSamples(
    "ParallelForBuildRun(32768 items)", config_, taskflow_runner, thread_center_runner);
}

[[nodiscard]] auto runDynamicBuildRun(const BenchConfig& config_) -> ScenarioSamples
{
  constexpr std::size_t kBranchCount = 64;

  auto taskflow_executor = tf::Executor{config_.workers};
  auto thread_center =
    ThreadCenter::Center{ThreadCenter::ExecutorConfig{.workers = config_.workers}};

  auto taskflow_runner = [&] {
    tf::Taskflow taskflow;
    std::atomic_int sink{0};

    taskflow.emplace([&](tf::Subflow& subflow_) {
      for (std::size_t branch = 0; branch < kBranchCount; ++branch) {
        subflow_.emplace([&] {
          sink.fetch_add(1, std::memory_order_relaxed);
        });
      }
      subflow_.join();
    });

    taskflow_executor.run(taskflow).wait();
  };

  auto thread_center_runner = [&] {
    auto plan = thread_center.makePlan();
    std::atomic_int sink{0};

    static_cast<void>(plan.dynamicTask(
      {.name = "dynamic_root"}, [&](ThreadCenter::DynamicContext& dynamic_context_) {
        for (std::size_t branch = 0; branch < kBranchCount; ++branch) {
          static_cast<void>(dynamic_context_.task({.name = "dynamic_leaf"}, [&] {
            sink.fetch_add(1, std::memory_order_relaxed);
          }));
        }
        dynamic_context_.join();
      }));

    auto run_handle = thread_center.dispatch(plan);
    run_handle.wait();
  };

  return collectScenarioSamples(
    "DynamicBuildRun(64 branches)", config_, taskflow_runner, thread_center_runner);
}

[[nodiscard]] auto runPipelineReuseRun(const BenchConfig& config_) -> ScenarioSamples
{
  constexpr std::size_t kTokenCount = 128;

  auto taskflow_executor = tf::Executor{config_.workers};
  auto thread_center =
    ThreadCenter::Center{ThreadCenter::ExecutorConfig{.workers = config_.workers}};

  using PipeType = tf::Pipe<std::function<void(tf::Pipeflow&)>>;
  using PipelineType = tf::ScalablePipeline<typename std::vector<PipeType>::iterator>;

  std::atomic_int taskflow_steps{0};
  auto taskflow_pipes = std::vector<PipeType>{};
  taskflow_pipes.emplace_back(tf::PipeType::SERIAL, [&](tf::Pipeflow& pipeflow_) {
    if (pipeflow_.token() >= kTokenCount) {
      pipeflow_.stop();
      return;
    }
    taskflow_steps.fetch_add(1, std::memory_order_relaxed);
  });
  taskflow_pipes.emplace_back(tf::PipeType::PARALLEL, [&](tf::Pipeflow&) {
    taskflow_steps.fetch_add(1, std::memory_order_relaxed);
  });
  taskflow_pipes.emplace_back(tf::PipeType::SERIAL, [&](tf::Pipeflow&) {
    taskflow_steps.fetch_add(1, std::memory_order_relaxed);
  });

  auto taskflow_pipeline = PipelineType{2};
  taskflow_pipeline.reset(taskflow_pipes.begin(), taskflow_pipes.end());
  tf::Taskflow taskflow_graph;
  taskflow_graph.composed_of(taskflow_pipeline);

  auto frame_plan = thread_center.makeFramePlan();
  std::atomic_int thread_center_steps{0};
  static_cast<void>(frame_plan.pipelineTask(
    ThreadCenter::FramePhase::END_FRAME,
    {.name = "pipeline"},
    kTokenCount,
    [&](ThreadCenter::PipelineBuilder& pipeline_builder_) {
      pipeline_builder_.serialStage({.name = "s0"}, [&](ThreadCenter::PipelineTokenContext&) {
        thread_center_steps.fetch_add(1, std::memory_order_relaxed);
      });
      pipeline_builder_.parallelStage({.name = "s1"}, [&](ThreadCenter::PipelineTokenContext&) {
        thread_center_steps.fetch_add(1, std::memory_order_relaxed);
      });
      pipeline_builder_.serialStage({.name = "s2"}, [&](ThreadCenter::PipelineTokenContext&) {
        thread_center_steps.fetch_add(1, std::memory_order_relaxed);
      });
    }));
  auto compiled_frame = std::move(frame_plan).compile();

  auto taskflow_runner = [&] {
    taskflow_steps.store(0, std::memory_order_relaxed);
    taskflow_pipeline.reset();
    taskflow_executor.run(taskflow_graph).wait();
  };

  auto thread_center_runner = [&] {
    thread_center_steps.store(0, std::memory_order_relaxed);
    auto frame_instance = compiled_frame.instantiate();
    auto run_handle = thread_center.dispatchFrame(frame_instance);
    run_handle.wait();
    compiled_frame.recycle(std::move(frame_instance));
  };

  return collectScenarioSamples(
    "PipelineReuseRun(128 tokens)", config_, taskflow_runner, thread_center_runner);
}

[[nodiscard]] auto runFrameReuseRun(const BenchConfig& config_) -> ScenarioSamples
{
  constexpr std::size_t kItemCount = 4096;

  auto taskflow_executor = tf::Executor{config_.workers};
  auto thread_center =
    ThreadCenter::Center{ThreadCenter::ExecutorConfig{.workers = config_.workers}};
  auto values = std::vector<std::int32_t>(kItemCount, 1);

  tf::Taskflow taskflow_graph;
  std::atomic<std::int64_t> taskflow_sum{0};
  auto taskflow_begin = taskflow_graph.emplace([] {});
  auto taskflow_parallel = taskflow_graph.for_each_index(
    std::size_t{0}, values.size(), std::size_t{1}, [&](std::size_t index_) {
      taskflow_sum.fetch_add(values[index_], std::memory_order_relaxed);
    });
  auto taskflow_end = taskflow_graph.emplace([] {});
  taskflow_begin.precede(taskflow_parallel);
  taskflow_parallel.precede(taskflow_end);

  auto frame_plan = thread_center.makeFramePlan();
  std::atomic<std::int64_t> thread_center_sum{0};
  static_cast<void>(frame_plan.task(ThreadCenter::FramePhase::INPUT, {.name = "begin"}, [] {}));
  static_cast<void>(frame_plan.parallelFor(
    ThreadCenter::FramePhase::GAMEPLAY,
    std::size_t{0},
    values.size(),
    std::size_t{1},
    {.name = "pf", .task_kind = ThreadCenter::TaskKind::PARALLEL_FOR, .min_grain_size = 128},
    [&](std::size_t index_) {
      thread_center_sum.fetch_add(values[index_], std::memory_order_relaxed);
    }));
  static_cast<void>(frame_plan.task(ThreadCenter::FramePhase::END_FRAME, {.name = "end"}, [] {}));

  auto compiled_frame = std::move(frame_plan).compile();

  auto taskflow_runner = [&] {
    taskflow_sum.store(0, std::memory_order_relaxed);
    taskflow_executor.run(taskflow_graph).wait();
  };

  auto thread_center_runner = [&] {
    thread_center_sum.store(0, std::memory_order_relaxed);
    auto frame_instance = compiled_frame.instantiate();
    auto run_handle = thread_center.dispatchFrame(frame_instance);
    run_handle.wait();
    compiled_frame.recycle(std::move(frame_instance));
  };

  return collectScenarioSamples(
    "FrameReuseRun(phase+parallel_for)", config_, taskflow_runner, thread_center_runner);
}

using ScenarioFn = ScenarioSamples (*)(const BenchConfig&);

struct ScenarioEntry final
{
  std::string_view name{};
  ScenarioFn fn{nullptr};
};

[[nodiscard]] auto allScenarios() -> std::vector<ScenarioEntry>
{
  return {
    ScenarioEntry{.name = "ChainBuildRun", .fn = &runChainBuildRun},
    ScenarioEntry{.name = "FanOutFanInBuildRun", .fn = &runFanOutFanInBuildRun},
    ScenarioEntry{.name = "ParallelForBuildRun", .fn = &runParallelForBuildRun},
    ScenarioEntry{.name = "DynamicBuildRun", .fn = &runDynamicBuildRun},
    ScenarioEntry{.name = "PipelineReuseRun", .fn = &runPipelineReuseRun},
    ScenarioEntry{.name = "FrameReuseRun", .fn = &runFrameReuseRun},
  };
}

} // namespace

int main(int argc, char** argv)
{
  auto args = std::vector<std::string>{};
  args.reserve(static_cast<std::size_t>(argc));
  for (int index = 0; index < argc; ++index) {
    args.emplace_back(argv[index]);
  }

  BenchConfig config{
    .workers = std::max(2u, std::thread::hardware_concurrency() / 2u),
    .warmup_iterations = 20,
    .measure_iterations = 80,
    .repeats = 3,
  };

  auto list_only = false;
  if (!parseArgs(args, config, list_only)) {
    printUsage(args.empty() ? "thread_center_vs_taskflow_bench" : args.front());
    return 1;
  }

  const auto scenarios = allScenarios();
  if (list_only) {
    std::cout << "Available scenarios:\n";
    for (const auto& scenario : scenarios) {
      std::cout << "  " << scenario.name << '\n';
    }
    return 0;
  }

  std::cout << "ThreadCenter vs Taskflow Benchmark\n";
  std::cout << "workers=" << config.workers << " warmup=" << config.warmup_iterations
            << " measure=" << config.measure_iterations << " repeats=" << config.repeats << "\n";

  auto results = std::vector<ScenarioResult>{};
  for (const auto& scenario : scenarios) {
    if (!shouldRunScenario(config, scenario.name)) {
      continue;
    }

    auto scenario_samples = scenario.fn(config);
    results.push_back(summarizeScenario(scenario_samples));
  }

  if (results.empty()) {
    std::cerr
      << "No benchmark scenario selected. Use --list-scenarios to inspect available names.\n";
    return 2;
  }

  std::cout << "\n================ Benchmark Summary ================\n";
  for (const auto& result : results) {
    printScenarioResult(result);
  }

  if (config.csv_output_path.has_value()) {
    writeCsvReport(*config.csv_output_path, config, results);
    std::cout << "\nCSV report written to: " << config.csv_output_path->string() << '\n';
  }

  if (config.json_output_path.has_value()) {
    writeJsonReport(*config.json_output_path, config, results);
    std::cout << "JSON report written to: " << config.json_output_path->string() << '\n';
  }

  return 0;
}
