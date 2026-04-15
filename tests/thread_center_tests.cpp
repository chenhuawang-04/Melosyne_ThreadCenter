#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <iostream>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include "thread_center/thread_center.hpp"

namespace
{

struct TestContext final
{
  std::uint32_t passed{0};
  std::uint32_t failed{0};

  void check(bool condition_, std::string_view test_name_, std::string_view message_)
  {
    if (condition_) {
      ++passed;
      return;
    }

    ++failed;
    std::cerr << "[FAIL] " << test_name_ << " - " << message_ << '\n';
  }
};

void runBasicPlanOrderTest(TestContext& context_)
{
  constexpr auto kTestName = "BasicPlanOrder";

  ThreadCenter::Center center{ThreadCenter::ExecutorConfig{.workers = 4}};
  auto plan = center.makePlan();

  std::atomic_int order{0};
  bool second_task_observed_expected{false};

  auto first_task = plan.task({.name = "first"}, [&] {
    order.store(1, std::memory_order_relaxed);
  });
  auto second_task = plan.task({.name = "second"}, [&] {
    second_task_observed_expected = order.load(std::memory_order_relaxed) == 1;
    order.store(2, std::memory_order_relaxed);
  });

  plan.precede(first_task, second_task);
  auto run_handle = center.dispatch(plan);
  run_handle.wait();

  context_.check(
    second_task_observed_expected, kTestName, "second task should observe first task result");
  context_.check(order.load(std::memory_order_relaxed) == 2, kTestName, "final order should be 2");
}

void runParallelForSumTest(TestContext& context_)
{
  constexpr auto kTestName = "ParallelForSum";

  ThreadCenter::Center center{ThreadCenter::ExecutorConfig{.workers = 4}};
  auto plan = center.makePlan();

  std::vector<std::int32_t> values(4096, 2);
  std::atomic<std::int64_t> sum{0};

  static_cast<void>(plan.parallelFor({.name = "parallel_sum",
                                      .task_kind = ThreadCenter::TaskKind::PARALLEL_FOR,
                                      .min_grain_size = 64},
                                     std::size_t{0},
                                     values.size(),
                                     std::size_t{1},
                                     [&](std::size_t index_) {
                                       sum.fetch_add(values[index_], std::memory_order_relaxed);
                                     }));

  auto run_handle = center.dispatch(plan);
  run_handle.wait();

  context_.check(sum.load(std::memory_order_relaxed) == 8192,
                 kTestName,
                 "parallel sum should match expected value");
}

void runConditionBranchTest(TestContext& context_)
{
  constexpr auto kTestName = "ConditionBranch";

  ThreadCenter::Center center{ThreadCenter::ExecutorConfig{.workers = 4}};
  auto plan = center.makePlan();

  std::atomic_int selected{-1};
  auto condition_task = plan.conditionTask({.name = "condition"}, [] {
    return 0;
  });
  auto branch_zero = plan.task({.name = "branch0"}, [&] {
    selected.store(0, std::memory_order_relaxed);
  });
  auto branch_one = plan.task({.name = "branch1"}, [&] {
    selected.store(1, std::memory_order_relaxed);
  });

  plan.precede(condition_task, branch_zero);
  plan.precede(condition_task, branch_one);

  auto run_handle = center.dispatch(plan);
  run_handle.wait();

  context_.check(selected.load(std::memory_order_relaxed) == 0,
                 kTestName,
                 "condition should select branch index 0");
}

void runDynamicTaskTest(TestContext& context_)
{
  constexpr auto kTestName = "DynamicTask";

  ThreadCenter::Center center{ThreadCenter::ExecutorConfig{.workers = 4}};
  auto plan = center.makePlan();

  std::atomic_int value{0};

  static_cast<void>(
    plan.dynamicTask({.name = "dynamic_root"}, [&](ThreadCenter::DynamicContext& dynamic_context_) {
      auto prepare_task = dynamic_context_.task({.name = "prepare"}, [&] {
        value.fetch_add(1, std::memory_order_relaxed);
      });
      auto commit_task = dynamic_context_.task({.name = "commit"}, [&] {
        value.fetch_add(2, std::memory_order_relaxed);
      });
      dynamic_context_.precede(prepare_task, commit_task);
      dynamic_context_.join();
    }));

  auto run_handle = center.dispatch(plan);
  run_handle.wait();

  context_.check(value.load(std::memory_order_relaxed) == 3,
                 kTestName,
                 "dynamic subgraph tasks should execute with dependency");
}

void runPipelineStopTest(TestContext& context_)
{
  constexpr auto kTestName = "PipelineStop";

  ThreadCenter::Center center{ThreadCenter::ExecutorConfig{.workers = 4}};
  auto plan = center.makePlan();

  std::atomic_int steps{0};

  static_cast<void>(plan.pipelineTask(
    {.name = "pipeline_root", .task_kind = ThreadCenter::TaskKind::PIPELINE},
    4,
    [&](ThreadCenter::PipelineBuilder& pipeline_builder_) {
      pipeline_builder_.serialStage({.name = "s0"},
                                    [&](ThreadCenter::PipelineTokenContext& token_context_) {
                                      steps.fetch_add(1, std::memory_order_relaxed);
                                      if (token_context_.tokenIndex() == 2) {
                                        token_context_.stop();
                                      }
                                    });
      pipeline_builder_.parallelStage({.name = "s1"},
                                      [&](ThreadCenter::PipelineTokenContext& token_context_) {
                                        if (token_context_.stopRequested()) {
                                          return;
                                        }
                                        steps.fetch_add(1, std::memory_order_relaxed);
                                      });
      pipeline_builder_.serialStage({.name = "s2"},
                                    [&](ThreadCenter::PipelineTokenContext& token_context_) {
                                      if (token_context_.stopRequested()) {
                                        return;
                                      }
                                      steps.fetch_add(1, std::memory_order_relaxed);
                                    });
    }));

  auto run_handle = center.dispatch(plan);
  run_handle.wait();

  context_.check(steps.load(std::memory_order_relaxed) == 7,
                 kTestName,
                 "pipeline should stop after token 2 stage 0");
}

void runCancelTokenTest(TestContext& context_)
{
  constexpr auto kTestName = "CancelToken";

  ThreadCenter::Center center{ThreadCenter::ExecutorConfig{.workers = 4}};
  auto plan = center.makePlan();

  std::atomic_int executed{0};
  static_cast<void>(
    plan.task({.name = "cancelled_task"}, [&](const ThreadCenter::CancelToken& cancel_token_) {
      if (!cancel_token_.stopRequested()) {
        executed.fetch_add(1, std::memory_order_relaxed);
      }
    }));

  ThreadCenter::CancelSource cancel_source;
  cancel_source.requestStop();

  auto run_handle = center.dispatch(plan, cancel_source.token());
  run_handle.wait();

  context_.check(executed.load(std::memory_order_relaxed) == 0,
                 kTestName,
                 "canceled task should not execute body");
}

void runFrameAutoBarrierTest(TestContext& context_)
{
  constexpr auto kTestName = "FrameAutoBarrier";

  ThreadCenter::Center center{ThreadCenter::ExecutorConfig{.workers = 4}};
  auto frame_plan = center.makeFramePlan();

  std::atomic_int phase_value{0};
  bool gameplay_observed_input{false};
  bool end_observed_gameplay{false};

  static_cast<void>(frame_plan.task(ThreadCenter::FramePhase::INPUT, {.name = "input"}, [&] {
    phase_value.store(1, std::memory_order_relaxed);
  }));

  static_cast<void>(frame_plan.task(ThreadCenter::FramePhase::GAMEPLAY, {.name = "gameplay"}, [&] {
    gameplay_observed_input = phase_value.load(std::memory_order_relaxed) == 1;
    phase_value.store(2, std::memory_order_relaxed);
  }));

  static_cast<void>(frame_plan.task(ThreadCenter::FramePhase::END_FRAME, {.name = "end"}, [&] {
    end_observed_gameplay = phase_value.load(std::memory_order_relaxed) == 2;
  }));

  auto compiled_frame = std::move(frame_plan).compile();
  auto frame_instance = compiled_frame.instantiate();
  auto run_handle = center.dispatchFrame(frame_instance);
  run_handle.wait();
  compiled_frame.recycle(std::move(frame_instance));

  context_.check(
    gameplay_observed_input, kTestName, "gameplay phase should observe input completion");
  context_.check(end_observed_gameplay, kTestName, "end phase should observe gameplay completion");
}

void runResourceDependencyInferenceTest(TestContext& context_)
{
  constexpr auto kTestName = "ResourceDependencyInference";

  ThreadCenter::Center center{ThreadCenter::ExecutorConfig{.workers = 4}};
  auto frame_plan = center.makeFramePlan();

  constexpr std::uint64_t kResourceId = 42;
  std::atomic_int state{0};
  bool reader_saw_write{false};

  static_cast<void>(frame_plan.systemTask(
    {.name = "writer",
     .phase = ThreadCenter::FramePhase::GAMEPLAY,
     .system_group = ThreadCenter::SystemGroup::GAMEPLAY,
     .task_desc = {.name = "writer_task"}},
    {{.resource_id = kResourceId, .mode = ThreadCenter::ResourceAccessMode::WRITE}},
    [&] {
      state.store(1, std::memory_order_relaxed);
    }));

  static_cast<void>(frame_plan.systemTask(
    {.name = "reader",
     .phase = ThreadCenter::FramePhase::GAMEPLAY,
     .system_group = ThreadCenter::SystemGroup::GAMEPLAY,
     .task_desc = {.name = "reader_task"}},
    {{.resource_id = kResourceId, .mode = ThreadCenter::ResourceAccessMode::READ}},
    [&] {
      reader_saw_write = state.load(std::memory_order_relaxed) == 1;
    }));

  auto compiled_frame = std::move(frame_plan).compile();
  auto frame_instance = compiled_frame.instantiate();
  auto run_handle = center.dispatchFrame(frame_instance);
  run_handle.wait();
  compiled_frame.recycle(std::move(frame_instance));

  context_.check(
    reader_saw_write, kTestName, "reader should be ordered after writer by resource dependency");
}

void runCompiledFrameReuseTest(TestContext& context_)
{
  constexpr auto kTestName = "CompiledFrameReuse";

  ThreadCenter::Center center{ThreadCenter::ExecutorConfig{.workers = 4}};
  auto frame_plan = center.makeFramePlan();

  std::atomic_int frame_counter{0};
  static_cast<void>(frame_plan.task(ThreadCenter::FramePhase::GAMEPLAY, {.name = "tick"}, [&] {
    frame_counter.fetch_add(1, std::memory_order_relaxed);
  }));

  auto compiled_frame = std::move(frame_plan).compile();

  auto frame_instance = compiled_frame.instantiate();
  auto run_handle = center.dispatchFrame(frame_instance);
  run_handle.wait();
  compiled_frame.recycle(std::move(frame_instance));

  auto reused_instance = compiled_frame.instantiate();
  auto reused_handle = center.dispatchFrame(reused_instance);
  reused_handle.wait();
  compiled_frame.recycle(std::move(reused_instance));

  context_.check(frame_counter.load(std::memory_order_relaxed) == 2,
                 kTestName,
                 "compiled frame should be reusable across dispatches");
}

void runProfileReportTest(TestContext& context_)
{
  constexpr auto kTestName = "ProfileReport";

  ThreadCenter::Center center{ThreadCenter::ExecutorConfig{.workers = 4}};
  auto frame_plan = center.makeFramePlan();

  auto task_a = frame_plan.task(
    ThreadCenter::FramePhase::INPUT, {.name = "A", .estimated_cost_ns = 10'000}, [] {});
  auto task_b = frame_plan.task(
    ThreadCenter::FramePhase::GAMEPLAY, {.name = "B", .estimated_cost_ns = 20'000}, [] {});
  frame_plan.precede(task_a, task_b);

  auto compiled_frame = std::move(frame_plan).compile();
  const auto& report = compiled_frame.profileReport();

  context_.check(report.node_count >= 2, kTestName, "profile should contain at least two nodes");
  context_.check(report.edge_count >= 1, kTestName, "profile should contain at least one edge");
  context_.check(report.critical_path.total_estimated_cost_ns >= 30'000,
                 kTestName,
                 "critical path cost should include both nodes");
}

void runRuntimeReportTest(TestContext& context_)
{
  constexpr auto kTestName = "RuntimeReport";

  ThreadCenter::Center center{ThreadCenter::ExecutorConfig{.workers = 4}};
  auto frame_plan = center.makeFramePlan();

  static_cast<void>(frame_plan.parallelFor(ThreadCenter::FramePhase::GAMEPLAY,
                                           std::size_t{0},
                                           std::size_t{2048},
                                           std::size_t{1},
                                           {.name = "runtime_pf",
                                            .task_kind = ThreadCenter::TaskKind::PARALLEL_FOR,
                                            .min_grain_size = 128},
                                           [](std::size_t) {}));

  auto compiled_frame = std::move(frame_plan).compile();
  auto frame_instance = compiled_frame.instantiate();
  auto run_handle = center.dispatchFrame(frame_instance);
  run_handle.wait();
  compiled_frame.recycle(std::move(frame_instance));

  const auto runtime_report = compiled_frame.runtimeReport();

  context_.check(runtime_report.dispatch_count == 1, kTestName, "dispatch_count should be 1");
  context_.check(
    !runtime_report.phase_profiles.empty(), kTestName, "phase runtime profiles should exist");
  context_.check(
    !runtime_report.lane_profiles.empty(), kTestName, "lane runtime profiles should exist");

  const auto worker_started_sum =
    std::accumulate(runtime_report.worker_profiles.begin(),
                    runtime_report.worker_profiles.end(),
                    std::uint64_t{0},
                    [](std::uint64_t value_, const ThreadCenter::WorkerRuntimeProfile& profile_) {
                      return value_ + profile_.started_count;
                    });
  context_.check(worker_started_sum > 0, kTestName, "worker profiles should record starts");
}

void runSnapshotExportTest(TestContext& context_)
{
  constexpr auto kTestName = "SnapshotExport";

  ThreadCenter::Center center{ThreadCenter::ExecutorConfig{.workers = 4}};
  auto frame_plan = center.makeFramePlan();

  static_cast<void>(frame_plan.task(ThreadCenter::FramePhase::INPUT, {.name = "snap"}, [] {}));

  auto compiled_frame = std::move(frame_plan).compile();
  auto frame_instance = compiled_frame.instantiate();
  auto run_handle = center.dispatchFrame(frame_instance);
  run_handle.wait();
  compiled_frame.recycle(std::move(frame_instance));

  const auto snapshot_json = ThreadCenter::Profiling::makeSnapshotJson(compiled_frame);

  context_.check(snapshot_json.find("\"static_profile\"") != std::string::npos,
                 kTestName,
                 "snapshot should include static_profile object");
  context_.check(snapshot_json.find("\"runtime_profile\"") != std::string::npos,
                 kTestName,
                 "snapshot should include runtime_profile object");
  context_.check(snapshot_json.find("\"worker_profiles\"") != std::string::npos,
                 kTestName,
                 "snapshot should include worker_profiles");
}

void runPlanClearTest(TestContext& context_)
{
  constexpr auto kTestName = "PlanClear";

  ThreadCenter::Center center{ThreadCenter::ExecutorConfig{.workers = 4}};
  auto plan = center.makePlan();

  std::atomic_int value{0};

  static_cast<void>(plan.task({.name = "first"}, [&] {
    value.store(1, std::memory_order_relaxed);
  }));
  auto run_handle = center.dispatch(plan);
  run_handle.wait();

  plan.clear();

  static_cast<void>(plan.task({.name = "second"}, [&] {
    value.store(2, std::memory_order_relaxed);
  }));
  auto second_handle = center.dispatch(plan);
  second_handle.wait();

  context_.check(value.load(std::memory_order_relaxed) == 2,
                 kTestName,
                 "plan.clear should allow rebuilding graph");
}

} // namespace

int main()
{
  TestContext context;

  runBasicPlanOrderTest(context);
  runParallelForSumTest(context);
  runConditionBranchTest(context);
  runDynamicTaskTest(context);
  runPipelineStopTest(context);
  runCancelTokenTest(context);
  runFrameAutoBarrierTest(context);
  runResourceDependencyInferenceTest(context);
  runCompiledFrameReuseTest(context);
  runProfileReportTest(context);
  runRuntimeReportTest(context);
  runSnapshotExportTest(context);
  runPlanClearTest(context);

  std::cout << "[ThreadCenter Tests] passed=" << context.passed << " failed=" << context.failed
            << '\n';

  return context.failed == 0 ? 0 : 1;
}
