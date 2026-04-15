#include <atomic>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

#include "thread_center/thread_center.hpp"

namespace
{

struct TraceStats final
{
  std::atomic_uint32_t submitted{0};
  std::atomic_uint32_t started{0};
  std::atomic_uint32_t finished{0};
  std::atomic_uint32_t canceled{0};
  std::atomic_uint32_t dispatch_begin{0};
  std::atomic_uint32_t dispatch_end{0};
  std::atomic_uint32_t pipeline_stage_begin{0};
  std::atomic_uint32_t pipeline_stage_end{0};
  std::atomic_uint32_t pipeline_stop_requested{0};
};

void onTaskEvent(void* user_data_, const ThreadCenter::TaskEvent& event_)
{
  auto* trace_stats = static_cast<TraceStats*>(user_data_);

  switch (event_.type) {
  case ThreadCenter::TaskEventType::SUBMITTED:
    trace_stats->submitted.fetch_add(1, std::memory_order_relaxed);
    break;
  case ThreadCenter::TaskEventType::STARTED:
    trace_stats->started.fetch_add(1, std::memory_order_relaxed);
    break;
  case ThreadCenter::TaskEventType::FINISHED:
    trace_stats->finished.fetch_add(1, std::memory_order_relaxed);
    break;
  case ThreadCenter::TaskEventType::CANCELED:
    trace_stats->canceled.fetch_add(1, std::memory_order_relaxed);
    break;
  }
}

void onDispatchEvent(void* user_data_, const ThreadCenter::DispatchEvent& event_)
{
  auto* trace_stats = static_cast<TraceStats*>(user_data_);

  switch (event_.type) {
  case ThreadCenter::DispatchEventType::BEGIN:
    trace_stats->dispatch_begin.fetch_add(1, std::memory_order_relaxed);
    break;
  case ThreadCenter::DispatchEventType::END:
    trace_stats->dispatch_end.fetch_add(1, std::memory_order_relaxed);
    break;
  }
}

void onPipelineEvent(void* user_data_, const ThreadCenter::PipelineEvent& event_)
{
  auto* trace_stats = static_cast<TraceStats*>(user_data_);

  switch (event_.type) {
  case ThreadCenter::PipelineEventType::STAGE_BEGIN:
    trace_stats->pipeline_stage_begin.fetch_add(1, std::memory_order_relaxed);
    break;
  case ThreadCenter::PipelineEventType::STAGE_END:
    trace_stats->pipeline_stage_end.fetch_add(1, std::memory_order_relaxed);
    break;
  case ThreadCenter::PipelineEventType::STOP_REQUESTED:
    trace_stats->pipeline_stop_requested.fetch_add(1, std::memory_order_relaxed);
    break;
  }
}

[[nodiscard]] auto framePhaseName(ThreadCenter::FramePhase phase_) -> const char*
{
  switch (phase_) {
  case ThreadCenter::FramePhase::INPUT:
    return "INPUT";
  case ThreadCenter::FramePhase::EARLY_UPDATE:
    return "EARLY_UPDATE";
  case ThreadCenter::FramePhase::GAMEPLAY:
    return "GAMEPLAY";
  case ThreadCenter::FramePhase::PHYSICS:
    return "PHYSICS";
  case ThreadCenter::FramePhase::ANIMATION:
    return "ANIMATION";
  case ThreadCenter::FramePhase::AI:
    return "AI";
  case ThreadCenter::FramePhase::CULLING:
    return "CULLING";
  case ThreadCenter::FramePhase::RENDER_PREP:
    return "RENDER_PREP";
  case ThreadCenter::FramePhase::RENDER_SUBMIT:
    return "RENDER_SUBMIT";
  case ThreadCenter::FramePhase::STREAMING_FINALIZE:
    return "STREAMING_FINALIZE";
  case ThreadCenter::FramePhase::END_FRAME:
    return "END_FRAME";
  }

  return "UNKNOWN_PHASE";
}

[[nodiscard]] auto executionLaneName(ThreadCenter::ExecutionLane lane_) -> const char*
{
  switch (lane_) {
  case ThreadCenter::ExecutionLane::FRAME_CRITICAL:
    return "FRAME_CRITICAL";
  case ThreadCenter::ExecutionLane::FRAME_NORMAL:
    return "FRAME_NORMAL";
  case ThreadCenter::ExecutionLane::BACKGROUND:
    return "BACKGROUND";
  case ThreadCenter::ExecutionLane::STREAMING:
    return "STREAMING";
  case ThreadCenter::ExecutionLane::BLOCKING:
    return "BLOCKING";
  case ThreadCenter::ExecutionLane::RENDER_ASSIST:
    return "RENDER_ASSIST";
  case ThreadCenter::ExecutionLane::SERVICE:
    return "SERVICE";
  }

  return "UNKNOWN_LANE";
}

} // namespace

int main()
{
  TraceStats trace_stats;

  ThreadCenter::Center center{
    ThreadCenter::ExecutorConfig{
      .workers = 4,
    },
    ThreadCenter::FrameMemoryConfig{
      .scratch_bytes = std::size_t{128} * std::size_t{1024},
    },
  };

  center.setTraceHooks(ThreadCenter::TraceHooks{
    .user_data = &trace_stats,
    .on_task_event = onTaskEvent,
    .on_dispatch_event = onDispatchEvent,
    .on_pipeline_event = onPipelineEvent,
  });

  auto frame_plan = center.makeFramePlan();
  frame_plan.reserveResourceStates(8);

  std::uint64_t world_state_resource = 1;
  std::uint64_t visibility_list_resource = 2;

  ThreadCenter::CancelSource cancel_source;
  std::atomic<std::int64_t> sum = 0;
  std::atomic<std::int64_t> visible_entities = 0;
  std::atomic<std::int32_t> selected_lod = -1;
  std::atomic<std::int32_t> dynamic_steps = 0;
  std::atomic<std::int32_t> pipeline_steps = 0;
  std::vector<std::uint32_t> inputs(1000, 1);

  auto begin_task = frame_plan.task(ThreadCenter::FramePhase::INPUT,
                                    {
                                      .name = "BeginFrame",
                                      .task_kind = ThreadCenter::TaskKind::COMPUTE,
                                      .lane = ThreadCenter::ExecutionLane::FRAME_CRITICAL,
                                      .priority = ThreadCenter::TaskPriority::CRITICAL,
                                      .budget_class = ThreadCenter::BudgetClass::REALTIME,
                                      .estimated_cost_ns = 5'000,
                                    },
                                    [] {
                                      std::cout << "Frame begin\n";
                                    });

  auto update_task = frame_plan.parallelForSystem(
    {
      .name = "UpdateActorsSystem",
      .phase = ThreadCenter::FramePhase::GAMEPLAY,
      .system_group = ThreadCenter::SystemGroup::GAMEPLAY,
      .task_desc =
        {
          .task_kind = ThreadCenter::TaskKind::PARALLEL_FOR,
          .lane = ThreadCenter::ExecutionLane::FRAME_NORMAL,
          .priority = ThreadCenter::TaskPriority::HIGH,
          .budget_class = ThreadCenter::BudgetClass::FRAME_BOUND,
          .max_concurrency = 4,
          .min_grain_size = 64,
          .estimated_cost_ns = 80'000,
        },
    },
    {
      ThreadCenter::ResourceAccessDesc{
        .resource_id = world_state_resource,
        .mode = ThreadCenter::ResourceAccessMode::WRITE,
      },
    },
    std::size_t{0},
    inputs.size(),
    std::size_t{1},
    [&](std::size_t index_, const ThreadCenter::CancelToken& cancel_token_) {
      if (cancel_token_.stopRequested()) {
        return;
      }

      sum.fetch_add(inputs[index_], std::memory_order_relaxed);
    });

  auto build_visibility_task = frame_plan.systemTask(
    {
      .name = "BuildVisibilitySystem",
      .phase = ThreadCenter::FramePhase::GAMEPLAY,
      .system_group = ThreadCenter::SystemGroup::RENDER,
      .task_desc =
        {
          .task_kind = ThreadCenter::TaskKind::COMPUTE,
          .lane = ThreadCenter::ExecutionLane::FRAME_NORMAL,
          .priority = ThreadCenter::TaskPriority::HIGH,
          .budget_class = ThreadCenter::BudgetClass::FRAME_BOUND,
          .estimated_cost_ns = 20'000,
        },
    },
    {
      ThreadCenter::ResourceAccessDesc{
        .resource_id = world_state_resource,
        .mode = ThreadCenter::ResourceAccessMode::READ,
      },
      ThreadCenter::ResourceAccessDesc{
        .resource_id = visibility_list_resource,
        .mode = ThreadCenter::ResourceAccessMode::WRITE,
      },
    },
    [&] {
      visible_entities.store(sum.load(std::memory_order_relaxed), std::memory_order_relaxed);
    });

  auto gameplay_barrier = frame_plan.phaseBarrier(ThreadCenter::FramePhase::GAMEPLAY,
                                                  {
                                                    .name = "GameplayBarrier",
                                                    .estimated_cost_ns = 3'000,
                                                  });

  auto submit_gate = frame_plan.mainThreadGate(ThreadCenter::FramePhase::RENDER_SUBMIT,
                                               {
                                                 .name = "RenderSubmitGate",
                                                 .estimated_cost_ns = 10'000,
                                               });

  auto end_task = frame_plan.task(
    ThreadCenter::FramePhase::END_FRAME,
    {
      .name = "EndFrame",
      .task_kind = ThreadCenter::TaskKind::COMPUTE,
      .lane = ThreadCenter::ExecutionLane::FRAME_CRITICAL,
      .priority = ThreadCenter::TaskPriority::CRITICAL,
      .budget_class = ThreadCenter::BudgetClass::REALTIME,
      .estimated_cost_ns = 5'000,
    },
    [&] {
      std::cout << "Frame end, sum = " << sum.load(std::memory_order_relaxed) << '\n';
      std::cout << "Visible entities = " << visible_entities.load(std::memory_order_relaxed)
                << '\n';
      std::cout << "UpdateActors blocking? "
                << ThreadCenter::isBlockingTask({
                     .lane = ThreadCenter::ExecutionLane::FRAME_NORMAL,
                   })
                << '\n';
      std::cout << "RenderSubmitGate gate? "
                << ThreadCenter::isGateTask({
                     .task_kind = ThreadCenter::TaskKind::GATE,
                     .flags = ThreadCenter::TaskFlags::MAIN_THREAD_GATE,
                   })
                << '\n';
      std::cout << "GameplayBarrier auto-wired? "
                << ThreadCenter::isGateTask({
                     .task_kind = ThreadCenter::TaskKind::GATE,
                   })
                << '\n';
    });

  auto lod_condition = frame_plan.conditionTask(
    ThreadCenter::FramePhase::END_FRAME,
    {
      .name = "LodCondition",
      .priority = ThreadCenter::TaskPriority::HIGH,
      .estimated_cost_ns = 1'000,
    },
    [&] {
      return visible_entities.load(std::memory_order_relaxed) > 900 ? 0 : 1;
    });

  auto high_lod_task = frame_plan.task(ThreadCenter::FramePhase::END_FRAME,
                                       {
                                         .name = "HighLodPath",
                                         .task_kind = ThreadCenter::TaskKind::COMPUTE,
                                         .estimated_cost_ns = 1'000,
                                       },
                                       [&] {
                                         selected_lod.store(0, std::memory_order_relaxed);
                                       });

  auto low_lod_task = frame_plan.task(ThreadCenter::FramePhase::END_FRAME,
                                      {
                                        .name = "LowLodPath",
                                        .task_kind = ThreadCenter::TaskKind::COMPUTE,
                                        .estimated_cost_ns = 1'000,
                                      },
                                      [&] {
                                        selected_lod.store(1, std::memory_order_relaxed);
                                      });

  auto summary_dynamic_task =
    frame_plan.dynamicTask(ThreadCenter::FramePhase::END_FRAME,
                           {
                             .name = "SummaryDynamicTask",
                             .estimated_cost_ns = 2'000,
                           },
                           [&](ThreadCenter::DynamicContext& dynamic_context_) {
                             auto prepare_task = dynamic_context_.task(
                               {
                                 .name = "SummaryPrepare",
                                 .task_kind = ThreadCenter::TaskKind::COMPUTE,
                                 .estimated_cost_ns = 500,
                               },
                               [&] {
                                 dynamic_steps.fetch_add(1, std::memory_order_relaxed);
                               });

                             auto commit_task = dynamic_context_.task(
                               {
                                 .name = "SummaryCommit",
                                 .task_kind = ThreadCenter::TaskKind::COMPUTE,
                                 .estimated_cost_ns = 500,
                               },
                               [&] {
                                 dynamic_steps.fetch_add(1, std::memory_order_relaxed);
                               });

                             dynamic_context_.precede(prepare_task, commit_task);
                             dynamic_context_.join();
                           });

  auto post_pipeline_task =
    frame_plan.pipelineTask(ThreadCenter::FramePhase::END_FRAME,
                            {
                              .name = "PostPipelineTask",
                              .estimated_cost_ns = 3'000,
                            },
                            3,
                            [&](ThreadCenter::PipelineBuilder& pipeline_builder_) {
                              pipeline_builder_.serialStage(
                                {
                                  .name = "PipelinePrepare",
                                },
                                [&](ThreadCenter::PipelineTokenContext& token_context_) {
                                  pipeline_steps.fetch_add(1, std::memory_order_relaxed);
                                  if (token_context_.tokenIndex() == 2) {
                                    token_context_.stop();
                                  }
                                });

                              pipeline_builder_.parallelStage(
                                {
                                  .name = "PipelineParallel",
                                },
                                [&](ThreadCenter::PipelineTokenContext& token_context_) {
                                  if (token_context_.stopRequested()) {
                                    return;
                                  }
                                  pipeline_steps.fetch_add(1, std::memory_order_relaxed);
                                });

                              pipeline_builder_.serialStage(
                                {
                                  .name = "PipelineFinalize",
                                },
                                [&](ThreadCenter::PipelineTokenContext& token_context_) {
                                  if (token_context_.stopRequested()) {
                                    return;
                                  }
                                  pipeline_steps.fetch_add(1, std::memory_order_relaxed);
                                });
                            });

  frame_plan.precede(end_task, lod_condition);
  frame_plan.precede(lod_condition, high_lod_task);
  frame_plan.precede(lod_condition, low_lod_task);
  frame_plan.precede(end_task, summary_dynamic_task);
  frame_plan.precede(end_task, post_pipeline_task);

  static_cast<void>(begin_task);
  static_cast<void>(update_task);
  static_cast<void>(build_visibility_task);
  static_cast<void>(gameplay_barrier);
  static_cast<void>(submit_gate);
  static_cast<void>(end_task);
  static_cast<void>(lod_condition);
  static_cast<void>(high_lod_task);
  static_cast<void>(low_lod_task);
  static_cast<void>(summary_dynamic_task);
  static_cast<void>(post_pipeline_task);

  auto compiled_frame = std::move(frame_plan).compile();
  const auto& profile_report = compiled_frame.profileReport();

  auto frame_instance = compiled_frame.instantiate();
  auto run_handle = center.dispatchFrame(frame_instance, cancel_source.token());
  run_handle.wait();
  compiled_frame.recycle(std::move(frame_instance));

  sum.store(0, std::memory_order_relaxed);
  visible_entities.store(0, std::memory_order_relaxed);
  selected_lod.store(-1, std::memory_order_relaxed);
  dynamic_steps.store(0, std::memory_order_relaxed);
  pipeline_steps.store(0, std::memory_order_relaxed);

  auto reused_frame_instance = compiled_frame.instantiate();
  auto reused_run_handle = center.dispatchFrame(reused_frame_instance, cancel_source.token());
  reused_run_handle.wait();
  compiled_frame.recycle(std::move(reused_frame_instance));

  auto runtime_report = compiled_frame.runtimeReport();
  auto snapshot_json = ThreadCenter::Profiling::makeSnapshotJson(compiled_frame);

  std::cout << "Submitted tasks = " << trace_stats.submitted.load(std::memory_order_relaxed)
            << '\n';
  std::cout << "Started events = " << trace_stats.started.load(std::memory_order_relaxed) << '\n';
  std::cout << "Finished events = " << trace_stats.finished.load(std::memory_order_relaxed) << '\n';
  std::cout << "Canceled events = " << trace_stats.canceled.load(std::memory_order_relaxed) << '\n';
  std::cout << "Selected LOD = " << selected_lod.load(std::memory_order_relaxed) << '\n';
  std::cout << "Dynamic steps = " << dynamic_steps.load(std::memory_order_relaxed) << '\n';
  std::cout << "Pipeline steps = " << pipeline_steps.load(std::memory_order_relaxed) << '\n';
  std::cout << "Pipeline stage begin = "
            << trace_stats.pipeline_stage_begin.load(std::memory_order_relaxed)
            << ", stage end = " << trace_stats.pipeline_stage_end.load(std::memory_order_relaxed)
            << ", stop requested = "
            << trace_stats.pipeline_stop_requested.load(std::memory_order_relaxed) << '\n';
  std::cout << "Dispatch begin = " << trace_stats.dispatch_begin.load(std::memory_order_relaxed)
            << ", dispatch end = " << trace_stats.dispatch_end.load(std::memory_order_relaxed)
            << '\n';
  std::cout << "Profile nodes = " << profile_report.node_count
            << ", edges = " << profile_report.edge_count << '\n';
  for (const auto& phase_profile : profile_report.phase_profiles) {
    std::cout << "Phase[" << framePhaseName(phase_profile.phase)
              << "] tasks=" << phase_profile.task_count << ", gates=" << phase_profile.gate_count
              << ", cost_ns=" << phase_profile.estimated_cost_ns << '\n';
  }
  for (const auto& lane_profile : profile_report.lane_profiles) {
    std::cout << "Lane[" << executionLaneName(lane_profile.lane)
              << "] tasks=" << lane_profile.task_count
              << ", cost_ns=" << lane_profile.estimated_cost_ns << '\n';
  }
  std::cout << "Critical path cost_ns = " << profile_report.critical_path.total_estimated_cost_ns
            << '\n';
  std::cout << "Critical path nodes = ";
  for (std::size_t node_index = 0; node_index < profile_report.critical_path.nodes.size();
       ++node_index) {
    const auto& critical_node = profile_report.critical_path.nodes[node_index];
    if (node_index != 0) {
      std::cout << " -> ";
    }
    std::cout << critical_node.name << "(" << framePhaseName(critical_node.phase) << ")";
  }
  std::cout << '\n';
  std::cout << "Snapshot json bytes = " << snapshot_json.size() << '\n';
  std::cout << "Runtime dispatch count = " << runtime_report.dispatch_count << '\n';
  for (const auto& phase_runtime : runtime_report.phase_profiles) {
    std::cout << "RuntimePhase[" << framePhaseName(phase_runtime.phase)
              << "] started=" << phase_runtime.started_count
              << ", finished=" << phase_runtime.finished_count
              << ", canceled=" << phase_runtime.canceled_count
              << ", begin_events=" << phase_runtime.begin_event_count
              << ", end_events=" << phase_runtime.end_event_count
              << ", last_begin_ns=" << phase_runtime.last_begin_offset_ns
              << ", last_end_ns=" << phase_runtime.last_end_offset_ns << '\n';
  }
  for (const auto& lane_runtime : runtime_report.lane_profiles) {
    std::cout << "RuntimeLane[" << executionLaneName(lane_runtime.lane)
              << "] started=" << lane_runtime.started_count
              << ", finished=" << lane_runtime.finished_count
              << ", canceled=" << lane_runtime.canceled_count << '\n';
  }
  for (const auto& worker_runtime : runtime_report.worker_profiles) {
    std::cout << "RuntimeWorker[" << worker_runtime.worker_id
              << "] started=" << worker_runtime.started_count
              << ", finished=" << worker_runtime.finished_count
              << ", max_queue=" << worker_runtime.max_queue_size_observed
              << ", busy_ns=" << worker_runtime.busy_time_ns
              << ", last_entry_ns=" << worker_runtime.last_entry_offset_ns
              << ", last_exit_ns=" << worker_runtime.last_exit_offset_ns
              << ", utilization_permille=" << worker_runtime.utilization_permille << '\n';
  }
  std::cout << "Runtime pipeline stage begin = "
            << runtime_report.pipeline_profile.stage_begin_count
            << ", stage end = " << runtime_report.pipeline_profile.stage_end_count
            << ", stop requested = " << runtime_report.pipeline_profile.stop_requested_count
            << '\n';

  return 0;
}
