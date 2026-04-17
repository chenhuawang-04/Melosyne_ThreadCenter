module;

#include <algorithm>
#include <array>
#include <atomic>
#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <memory_resource>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <taskflow/algorithm/pipeline.hpp>
#include <taskflow/taskflow.hpp>

export module Center.Thread.Plan;

import Center.Thread.Common;

export {
namespace ThreadCenter
{

template <Detail::BackendAdapter Backend> class BasicPlan;
template <Detail::BackendAdapter Backend> class BasicCompiledFramePlan;
template <Detail::BackendAdapter Backend> class BasicDynamicContext;
template <Detail::BackendAdapter Backend> class BasicFrameInstance;
template <Detail::BackendAdapter Backend> class BasicFramePlan;

template <Detail::BackendAdapter Backend> class BasicCenter;

template <Detail::BackendAdapter Backend> class BasicGateHandle;

template <Detail::BackendAdapter Backend> class BasicTaskHandle final
{
public:
  BasicTaskHandle() = default;

private:
  using NativeType = typename Backend::NodeType;

  explicit BasicTaskHandle(NativeType native_, std::uint32_t node_id_) noexcept
      : native_(native_), node_id_(node_id_)
  {
  }

  NativeType native_{};
  std::uint32_t node_id_{0};

  friend class BasicDynamicContext<Backend>;
  friend class BasicPlan<Backend>;
  friend class BasicFramePlan<Backend>;
};

template <Detail::BackendAdapter Backend> class BasicRunHandle final
{
public:
  BasicRunHandle() = default;

  void wait()
  {
    Backend::wait(native_);

    if (!dispatch_end_emitted_) {
      Detail::emitDispatchEvent(trace_hooks_, DispatchEventType::END, worker_count_);
      dispatch_end_emitted_ = true;
    }

    if (wait_complete_callback_ != nullptr) {
      wait_complete_callback_(wait_complete_user_data_);
      wait_complete_callback_ = nullptr;
      wait_complete_user_data_ = nullptr;
    }
  }

private:
  using NativeType = typename Backend::RunHandleType;

  explicit BasicRunHandle(NativeType native_,
                          const TraceHooks& trace_hooks_,
                          std::uint32_t worker_count_,
                          void (*wait_complete_callback_)(void*) = nullptr,
                          void* wait_complete_user_data_ = nullptr)
      : native_(std::move(native_)), trace_hooks_(trace_hooks_), worker_count_(worker_count_),
        wait_complete_callback_(wait_complete_callback_),
        wait_complete_user_data_(wait_complete_user_data_)
  {
  }

  NativeType native_{};
  TraceHooks trace_hooks_{};
  std::uint32_t worker_count_{0};
  void (*wait_complete_callback_)(void*){nullptr};
  void* wait_complete_user_data_{nullptr};
  bool dispatch_end_emitted_{false};

  friend class BasicCenter<Backend>;
};

template <Detail::BackendAdapter Backend> class BasicGateHandle final
{
public:
  BasicGateHandle() = default;

private:
  using NativeType = typename Backend::NodeType;

  explicit BasicGateHandle(NativeType native_, std::uint32_t node_id_) noexcept
      : native_(native_), node_id_(node_id_)
  {
  }

  NativeType native_{};
  std::uint32_t node_id_{0};

  friend class BasicPlan<Backend>;
  friend class BasicFramePlan<Backend>;
};

template <Detail::BackendAdapter Backend> class BasicDynamicContext final
{
public:
  using TaskHandle = BasicTaskHandle<Backend>;
  using GateHandle = BasicGateHandle<Backend>;

  template <class Fn>
    requires std::invocable<Fn&> || std::invocable<Fn&, const CancelToken&>
  auto task(const TaskDesc& desc_, Fn&& fn_) -> TaskHandle
  {
    auto task_handle = TaskHandle{
      Backend::emplace(
        subflow_, desc_, Detail::makeTaskInvoker(desc_, runtime_state_, std::forward<Fn>(fn_))),
      allocateNodeId()};
    if (runtime_state_->trace_hooks.on_task_event != nullptr) {
      Detail::emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::SUBMITTED);
    }
    return task_handle;
  }

  template <class Fn>
    requires(std::invocable<Fn&> && std::integral<std::invoke_result_t<Fn&>>) ||
            (std::invocable<Fn&, const CancelToken&> &&
             std::integral<std::invoke_result_t<Fn&, const CancelToken&>>)
  auto conditionTask(const TaskDesc& desc_, Fn&& fn_) -> TaskHandle
  {
    auto condition_desc = desc_;
    condition_desc.task_kind = TaskKind::CONDITION;
    auto task_handle =
      TaskHandle{Backend::emplace(subflow_,
                                  condition_desc,
                                  Detail::makeConditionInvoker(
                                    condition_desc, runtime_state_, std::forward<Fn>(fn_))),
                 allocateNodeId()};
    if (runtime_state_->trace_hooks.on_task_event != nullptr) {
      Detail::emitTaskEvent(runtime_state_->trace_hooks, condition_desc, TaskEventType::SUBMITTED);
    }
    return task_handle;
  }

  auto gate(const TaskDesc& desc_) -> GateHandle
  {
    auto gate_desc = Detail::normalizeGateDesc(desc_);
    auto gate_handle =
      GateHandle{Backend::emplace(
                   subflow_, gate_desc, Detail::makeTaskInvoker(gate_desc, runtime_state_, [] {})),
                 allocateNodeId()};
    if (runtime_state_->trace_hooks.on_task_event != nullptr) {
      Detail::emitTaskEvent(runtime_state_->trace_hooks, gate_desc, TaskEventType::SUBMITTED);
    }
    return gate_handle;
  }

  template <std::integral Index, class Fn>
    requires std::invocable<Fn&, Index> || std::invocable<Fn&, Index, const CancelToken&>
  auto parallelFor(const TaskDesc& desc_, Index first_, Index last_, Index step_, Fn&& fn_)
    -> TaskHandle
  {
    const auto batch_size = Detail::parallelForBatchSize(desc_);
    auto task_handle = [&]() {
      if constexpr (std::same_as<Backend, Detail::TaskflowBackend>) {
        if (batch_size > 1) {
          return TaskHandle{
            Backend::parallelFor(
              subflow_,
              desc_,
              first_,
              last_,
              Detail::makeParallelForBatchStride(step_, batch_size),
              Detail::makeBatchedIndexedTaskInvoker<Index>(
                desc_, runtime_state_, last_, step_, batch_size, std::forward<Fn>(fn_))),
            allocateNodeId()};
        }
      }

      return TaskHandle{Backend::parallelFor(subflow_,
                                             desc_,
                                             first_,
                                             last_,
                                             step_,
                                             Detail::makeIndexedTaskInvoker<Index>(
                                               desc_, runtime_state_, std::forward<Fn>(fn_))),
                        allocateNodeId()};
    }();
    if (runtime_state_->trace_hooks.on_task_event != nullptr) {
      Detail::emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::SUBMITTED);
    }
    return task_handle;
  }

  void precede(TaskHandle from_, TaskHandle to_)
  {
    Backend::depend(from_.native_, to_.native_);
  }

  void precede(TaskHandle from_, GateHandle to_)
  {
    Backend::depend(from_.native_, to_.native_);
  }

  void precede(GateHandle from_, TaskHandle to_)
  {
    Backend::depend(from_.native_, to_.native_);
  }

  void precede(GateHandle from_, GateHandle to_)
  {
    Backend::depend(from_.native_, to_.native_);
  }

  void join()
  {
    Backend::join(subflow_);
  }

private:
  using SubflowType = typename Backend::SubflowType;

  BasicDynamicContext(SubflowType& subflow_,
                      const std::shared_ptr<Detail::PlanRuntimeState>& runtime_state_,
                      std::uint32_t* next_node_id_) noexcept
      : subflow_(subflow_), runtime_state_(runtime_state_), next_node_id_(next_node_id_)
  {
  }

  [[nodiscard]] auto allocateNodeId() noexcept -> std::uint32_t
  {
    auto node_id = *next_node_id_;
    ++(*next_node_id_);
    return node_id;
  }

  SubflowType& subflow_;
  std::shared_ptr<Detail::PlanRuntimeState> runtime_state_{};
  std::uint32_t* next_node_id_{nullptr};

  friend class BasicPlan<Backend>;
};

template <Detail::BackendAdapter Backend> class BasicPlan final
{
public:
  using BackendType = Backend;
  using TaskHandle = BasicTaskHandle<Backend>;
  using GateHandle = BasicGateHandle<Backend>;

  explicit BasicPlan(const TraceHooks& trace_hooks_)
      : native_(Backend::makeGraph()), runtime_state_(Detail::makePlanRuntimeState(trace_hooks_))
  {
  }

  BasicPlan(const BasicPlan&) = delete;
  auto operator=(const BasicPlan&) -> BasicPlan& = delete;
  BasicPlan(BasicPlan&&) noexcept = default;
  auto operator=(BasicPlan&&) noexcept -> BasicPlan& = default;

  template <class Fn>
    requires std::invocable<Fn&> || std::invocable<Fn&, const CancelToken&>
  auto task(const TaskDesc& desc_, Fn&& fn_) -> TaskHandle
  {
    auto task_handle = TaskHandle{
      Backend::emplace(
        native_, desc_, Detail::makeTaskInvoker(desc_, runtime_state_, std::forward<Fn>(fn_))),
      allocateNodeId()};
    if (runtime_state_->trace_hooks.on_task_event != nullptr) {
      Detail::emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::SUBMITTED);
    }
    return task_handle;
  }

  template <class Fn>
    requires(std::invocable<Fn&> && std::integral<std::invoke_result_t<Fn&>>) ||
            (std::invocable<Fn&, const CancelToken&> &&
             std::integral<std::invoke_result_t<Fn&, const CancelToken&>>)
  auto conditionTask(const TaskDesc& desc_, Fn&& fn_) -> TaskHandle
  {
    auto condition_desc = desc_;
    condition_desc.task_kind = TaskKind::CONDITION;
    auto task_handle =
      TaskHandle{Backend::emplace(native_,
                                  condition_desc,
                                  Detail::makeConditionInvoker(
                                    condition_desc, runtime_state_, std::forward<Fn>(fn_))),
                 allocateNodeId()};
    if (runtime_state_->trace_hooks.on_task_event != nullptr) {
      Detail::emitTaskEvent(runtime_state_->trace_hooks, condition_desc, TaskEventType::SUBMITTED);
    }
    return task_handle;
  }

  template <class Fn>
    requires std::invocable<Fn&, BasicDynamicContext<Backend>&> ||
             std::invocable<Fn&, BasicDynamicContext<Backend>&, const CancelToken&>
  auto dynamicTask(const TaskDesc& desc_, Fn&& fn_) -> TaskHandle
  {
    auto dynamic_desc = desc_;
    dynamic_desc.task_kind = TaskKind::DYNAMIC;
    auto task_handle = TaskHandle{
      Backend::emplace(
        native_,
        dynamic_desc,
        [dynamic_desc,
         runtime_state = runtime_state_,
         next_node_id = &next_node_id_,
         fn = std::forward<Fn>(fn_)](typename Backend::SubflowType& subflow_) mutable {
          auto cancel_token = CancelToken{runtime_state->cancel_state};

          if (Detail::shouldCancel(dynamic_desc, cancel_token)) {
            Detail::emitTaskEvent(
              runtime_state->trace_hooks, dynamic_desc, TaskEventType::CANCELED);
            return;
          }

          Detail::emitTaskEvent(runtime_state->trace_hooks, dynamic_desc, TaskEventType::STARTED);
          auto dynamic_context =
            BasicDynamicContext<Backend>{subflow_, runtime_state, next_node_id};

          if constexpr (std::invocable<Fn&, BasicDynamicContext<Backend>&, const CancelToken&>) {
            fn(dynamic_context, cancel_token);
          }
          else {
            fn(dynamic_context);
          }

          Detail::emitTaskEvent(runtime_state->trace_hooks, dynamic_desc, TaskEventType::FINISHED);
        }),
      allocateNodeId()};
    if (runtime_state_->trace_hooks.on_task_event != nullptr) {
      Detail::emitTaskEvent(runtime_state_->trace_hooks, dynamic_desc, TaskEventType::SUBMITTED);
    }
    return task_handle;
  }

  template <class BuildFn>
    requires std::invocable<BuildFn&, PipelineBuilder&>
  auto pipelineTask(const TaskDesc& desc_, std::size_t token_count_, BuildFn&& build_fn_)
    -> TaskHandle
  {
    auto pipeline_desc = desc_;
    pipeline_desc.task_kind = TaskKind::PIPELINE;

    PipelineBuilder pipeline_builder;
    build_fn_(pipeline_builder);
    auto stage_specs = Detail::makeTracedPipelineStageSpecs(
      pipeline_desc, runtime_state_, std::move(pipeline_builder).releaseStages());

    if constexpr (std::same_as<Backend, Detail::TaskflowBackend>) {
      using PipeType = tf::Pipe<std::function<void(tf::Pipeflow&)>>;
      using PipelineType = tf::ScalablePipeline<typename std::vector<PipeType>::iterator>;

      struct PipelineState final
      {
        std::shared_ptr<Detail::PlanRuntimeState> runtime_state;
        std::shared_ptr<std::atomic_bool> stop_requested{std::make_shared<std::atomic_bool>(false)};
        std::vector<PipelineBuilder::StageSpec> stage_specs;
        std::vector<PipeType> pipes;
        PipelineType pipeline;
        std::size_t token_count{0};

        PipelineState(std::size_t line_count_,
                      std::shared_ptr<Detail::PlanRuntimeState> runtime_state_,
                      std::vector<PipelineBuilder::StageSpec> stage_specs_,
                      std::size_t token_count_)
            : runtime_state(std::move(runtime_state_)), stage_specs(std::move(stage_specs_)),
              pipeline(line_count_), token_count(token_count_)
        {
          buildPipes();
          pipeline.reset(pipes.begin(), pipes.end());
        }

        void prepareForDispatch()
        {
          stop_requested->store(false, std::memory_order_relaxed);
          pipeline.reset();
        }

        void buildPipes()
        {
          pipes.emplace_back(
            tf::PipeType::SERIAL,
            [runtime_state = runtime_state,
             stop_requested = stop_requested,
             token_count = token_count](tf::Pipeflow& pipeflow_) {
              if ((runtime_state->cancel_state != nullptr &&
                   runtime_state->cancel_state->stop_requested.load(std::memory_order_relaxed)) ||
                  stop_requested->load(std::memory_order_relaxed) ||
                  pipeflow_.token() >= token_count) {
                pipeflow_.stop();
              }
            });

          for (std::size_t stage_index = 0; stage_index < stage_specs.size(); ++stage_index) {
            auto pipe_type = stage_specs[stage_index].desc.kind == PipelineStageKind::SERIAL
                               ? tf::PipeType::SERIAL
                               : tf::PipeType::PARALLEL;

            pipes.emplace_back(
              pipe_type,
              [runtime_state = runtime_state,
               stop_requested = stop_requested,
               stage_specs_ptr = &stage_specs,
               stage_index](tf::Pipeflow& pipeflow_) {
                auto cancel_token = CancelToken{runtime_state->cancel_state};
                if (cancel_token.stopRequested() ||
                    stop_requested->load(std::memory_order_relaxed)) {
                  return;
                }

                auto token_context =
                  PipelineTokenContext{pipeflow_.token(), stage_index, stop_requested};
                (*stage_specs_ptr)[stage_index].callback(token_context, cancel_token);
              });
          }
        }
      };

      if (stage_specs.empty() || token_count_ == 0) {
        return task(pipeline_desc, [] {});
      }

      const auto line_count = std::max<std::size_t>(
        1,
        pipeline_desc.max_concurrency == 0
          ? std::size_t{1}
          : std::min<std::size_t>(token_count_, pipeline_desc.max_concurrency));

      auto pipeline_state = std::make_shared<PipelineState>(
        line_count, runtime_state_, std::move(stage_specs), token_count_);
      auto task_handle =
        TaskHandle{native_.composed_of(pipeline_state->pipeline), allocateNodeId()};
      Backend::decorate(task_handle.native_, pipeline_desc);
      pre_dispatch_hooks_.push_back(
        Detail::PreDispatchHook{[](void* user_data_) {
                                  static_cast<PipelineState*>(user_data_)->prepareForDispatch();
                                },
                                pipeline_state.get()});
      backend_keepalives_.push_back(pipeline_state);
      if (runtime_state_->trace_hooks.on_task_event != nullptr) {
        Detail::emitTaskEvent(runtime_state_->trace_hooks, pipeline_desc, TaskEventType::SUBMITTED);
      }
      return task_handle;
    }
    else {
      return dynamicTask(
        pipeline_desc,
        [stage_specs = std::move(stage_specs),
         token_count_](BasicDynamicContext<Backend>& dynamic_context_, const CancelToken&) mutable {
          if (stage_specs.empty() || token_count_ == 0) {
            return;
          }

          auto stop_requested = std::make_shared<std::atomic_bool>(false);
          auto stage_tasks = std::vector<std::vector<TaskHandle>>(
            stage_specs.size(), std::vector<TaskHandle>(token_count_));

          for (std::size_t stage_index = 0; stage_index < stage_specs.size(); ++stage_index) {
            for (std::size_t token_index = 0; token_index < token_count_; ++token_index) {
              auto stage_desc =
                Detail::normalizePipelineStageTaskDesc(stage_specs[stage_index].desc);
              auto stage_task = dynamic_context_.task(
                stage_desc,
                [stage_specs_ptr = &stage_specs, stage_index, token_index, stop_requested](
                  const CancelToken& cancel_token_) {
                  if (cancel_token_.stopRequested() ||
                      stop_requested->load(std::memory_order_relaxed)) {
                    return;
                  }

                  auto token_context =
                    PipelineTokenContext{token_index, stage_index, stop_requested};
                  (*stage_specs_ptr)[stage_index].callback(token_context, cancel_token_);
                });

              if (stage_index > 0) {
                dynamic_context_.precede(stage_tasks[stage_index - 1][token_index], stage_task);
              }

              if (token_index > 0 &&
                  stage_specs[stage_index].desc.kind == PipelineStageKind::SERIAL) {
                dynamic_context_.precede(stage_tasks[stage_index][token_index - 1], stage_task);
              }

              stage_tasks[stage_index][token_index] = stage_task;
            }
          }

          dynamic_context_.join();
        });
    }
  }

  auto gate(const TaskDesc& desc_) -> GateHandle
  {
    auto gate_desc = Detail::normalizeGateDesc(desc_);
    auto gate_handle =
      GateHandle{Backend::emplace(
                   native_, gate_desc, Detail::makeTaskInvoker(gate_desc, runtime_state_, [] {})),
                 allocateNodeId()};
    if (runtime_state_->trace_hooks.on_task_event != nullptr) {
      Detail::emitTaskEvent(runtime_state_->trace_hooks, gate_desc, TaskEventType::SUBMITTED);
    }
    return gate_handle;
  }

  auto mainThreadGate(const TaskDesc& desc_) -> GateHandle
  {
    auto gate_desc = Detail::normalizeMainThreadGateDesc(desc_);
    auto gate_handle =
      GateHandle{Backend::emplace(
                   native_, gate_desc, Detail::makeTaskInvoker(gate_desc, runtime_state_, [] {})),
                 allocateNodeId()};
    if (runtime_state_->trace_hooks.on_task_event != nullptr) {
      Detail::emitTaskEvent(runtime_state_->trace_hooks, gate_desc, TaskEventType::SUBMITTED);
    }
    return gate_handle;
  }

  template <std::integral Index, class Fn>
    requires std::invocable<Fn&, Index> || std::invocable<Fn&, Index, const CancelToken&>
  auto parallelFor(const TaskDesc& desc_, Index first_, Index last_, Index step_, Fn&& fn_)
    -> TaskHandle
  {
    const auto batch_size = Detail::parallelForBatchSize(desc_);
    auto task_handle = [&]() {
      if constexpr (std::same_as<Backend, Detail::TaskflowBackend>) {
        if (batch_size > 1) {
          return TaskHandle{
            Backend::parallelFor(
              native_,
              desc_,
              first_,
              last_,
              Detail::makeParallelForBatchStride(step_, batch_size),
              Detail::makeBatchedIndexedTaskInvoker<Index>(
                desc_, runtime_state_, last_, step_, batch_size, std::forward<Fn>(fn_))),
            allocateNodeId()};
        }
      }

      return TaskHandle{Backend::parallelFor(native_,
                                             desc_,
                                             first_,
                                             last_,
                                             step_,
                                             Detail::makeIndexedTaskInvoker<Index>(
                                               desc_, runtime_state_, std::forward<Fn>(fn_))),
                        allocateNodeId()};
    }();
    if (runtime_state_->trace_hooks.on_task_event != nullptr) {
      Detail::emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::SUBMITTED);
    }
    return task_handle;
  }

  void precede(TaskHandle from_, TaskHandle to_)
  {
    Backend::depend(from_.native_, to_.native_);
  }

  void precede(TaskHandle from_, GateHandle to_)
  {
    Backend::depend(from_.native_, to_.native_);
  }

  void precede(GateHandle from_, TaskHandle to_)
  {
    Backend::depend(from_.native_, to_.native_);
  }

  void precede(GateHandle from_, GateHandle to_)
  {
    Backend::depend(from_.native_, to_.native_);
  }

  void clear()
  {
    Backend::clear(native_);
    pre_dispatch_hooks_.clear();
    backend_keepalives_.clear();
  }

private:
  void runPreDispatchHooks()
  {
    for (const auto& pre_dispatch_hook : pre_dispatch_hooks_) {
      pre_dispatch_hook.run();
    }
  }

  [[nodiscard]] auto allocateNodeId() noexcept -> std::uint32_t
  {
    return next_node_id_++;
  }

  typename Backend::GraphType native_;
  std::shared_ptr<Detail::PlanRuntimeState> runtime_state_{};
  std::vector<Detail::PreDispatchHook> pre_dispatch_hooks_{};
  std::vector<std::shared_ptr<void>> backend_keepalives_{};
  std::uint32_t next_node_id_{1};

  friend class BasicCenter<Backend>;
};

template <Detail::BackendAdapter Backend> class BasicCompiledFramePlan final
{
public:
  using PlanType = BasicPlan<Backend>;

  BasicCompiledFramePlan(const BasicCompiledFramePlan&) = delete;
  auto operator=(const BasicCompiledFramePlan&) -> BasicCompiledFramePlan& = delete;
  BasicCompiledFramePlan(BasicCompiledFramePlan&&) noexcept = default;
  auto operator=(BasicCompiledFramePlan&&) noexcept -> BasicCompiledFramePlan& = default;

  [[nodiscard]] auto instantiate() -> BasicFrameInstance<Backend>
  {
    return BasicFrameInstance<Backend>{std::move(*plan_), runtime_collector_};
  }

  void recycle(BasicFrameInstance<Backend>&& frame_instance_)
  {
    *plan_ = std::move(frame_instance_.plan_);
  }

  [[nodiscard]] auto profileReport() const noexcept -> const FrameProfileReport&
  {
    return *profile_report_;
  }

  [[nodiscard]] auto runtimeReport() const -> FrameRuntimeReport
  {
    return runtime_collector_->snapshot();
  }

private:
  explicit BasicCompiledFramePlan(PlanType&& plan_,
                                  FrameProfileReport&& profile_report_,
                                  std::shared_ptr<Detail::FrameRuntimeCollector> runtime_collector_)
      : plan_(std::make_unique<PlanType>(std::move(plan_))),
        profile_report_(std::make_shared<FrameProfileReport>(std::move(profile_report_))),
        runtime_collector_(std::move(runtime_collector_))
  {
  }

  std::unique_ptr<PlanType> plan_{};
  std::shared_ptr<FrameProfileReport> profile_report_{};
  std::shared_ptr<Detail::FrameRuntimeCollector> runtime_collector_{};

  friend class BasicFramePlan<Backend>;
};

template <Detail::BackendAdapter Backend> class BasicFrameInstance final
{
public:
  using PlanType = BasicPlan<Backend>;

  BasicFrameInstance(const BasicFrameInstance&) = delete;
  auto operator=(const BasicFrameInstance&) -> BasicFrameInstance& = delete;
  BasicFrameInstance(BasicFrameInstance&&) noexcept = default;
  auto operator=(BasicFrameInstance&&) noexcept -> BasicFrameInstance& = default;

private:
  explicit BasicFrameInstance(PlanType&& plan_,
                              std::shared_ptr<Detail::FrameRuntimeCollector> runtime_collector_)
      : plan_(std::move(plan_)), runtime_collector_(std::move(runtime_collector_))
  {
  }

  PlanType plan_;
  std::shared_ptr<Detail::FrameRuntimeCollector> runtime_collector_{};

  friend class BasicCompiledFramePlan<Backend>;
  friend class BasicCenter<Backend>;
};

} // namespace ThreadCenter

namespace ThreadCenter
{

template <Detail::BackendAdapter Backend> class BasicFramePlan final
{
public:
  using BackendType = Backend;
  using PlanType = BasicPlan<Backend>;
  using TaskHandle = BasicTaskHandle<Backend>;
  using GateHandle = BasicGateHandle<Backend>;

  BasicFramePlan(const TraceHooks& trace_hooks_, FrameMemoryConfig memory_config_)
      : plan_(trace_hooks_), memory_config_(memory_config_),
        scratch_arena_(std::make_shared<Detail::FrameScratchArena>(memory_config_)),
        phase_states_(scratch_arena_->resource()), resource_states_(scratch_arena_->resource()),
        profile_nodes_(scratch_arena_->resource()), profile_edges_(scratch_arena_->resource())
  {
    initializePhaseStates();
  }

  BasicFramePlan(const BasicFramePlan&) = delete;
  auto operator=(const BasicFramePlan&) -> BasicFramePlan& = delete;
  BasicFramePlan(BasicFramePlan&&) noexcept = default;
  auto operator=(BasicFramePlan&&) noexcept -> BasicFramePlan& = default;

  template <class Fn>
    requires std::invocable<Fn&> || std::invocable<Fn&, const CancelToken&>
  auto task(FramePhase phase_, const TaskDesc& desc_, Fn&& fn_) -> TaskHandle
  {
    auto task_desc = Detail::normalizeFrameTaskDesc(phase_, desc_);
    auto task_handle = plan_.task(task_desc, std::forward<Fn>(fn_));
    registerPhaseNode(phase_, task_handle, task_desc);
    return task_handle;
  }

  template <class Fn>
    requires(std::invocable<Fn&> && std::integral<std::invoke_result_t<Fn&>>) ||
            (std::invocable<Fn&, const CancelToken&> &&
             std::integral<std::invoke_result_t<Fn&, const CancelToken&>>)
  auto conditionTask(FramePhase phase_, const TaskDesc& desc_, Fn&& fn_) -> TaskHandle
  {
    auto task_desc = Detail::normalizeFrameTaskDesc(phase_, desc_);
    auto task_handle = plan_.conditionTask(task_desc, std::forward<Fn>(fn_));
    registerPhaseNode(phase_, task_handle, task_desc);
    return task_handle;
  }

  template <class Fn>
    requires std::invocable<Fn&, BasicDynamicContext<Backend>&> ||
             std::invocable<Fn&, BasicDynamicContext<Backend>&, const CancelToken&>
  auto dynamicTask(FramePhase phase_, const TaskDesc& desc_, Fn&& fn_) -> TaskHandle
  {
    auto task_desc = Detail::normalizeFrameTaskDesc(phase_, desc_);
    auto task_handle = plan_.dynamicTask(task_desc, std::forward<Fn>(fn_));
    registerPhaseNode(phase_, task_handle, task_desc);
    return task_handle;
  }

  template <class BuildFn>
    requires std::invocable<BuildFn&, PipelineBuilder&>
  auto pipelineTask(FramePhase phase_,
                    const TaskDesc& desc_,
                    std::size_t token_count_,
                    BuildFn&& build_fn_) -> TaskHandle
  {
    auto task_desc = Detail::normalizeFrameTaskDesc(phase_, desc_);
    auto task_handle =
      plan_.pipelineTask(task_desc, token_count_, std::forward<BuildFn>(build_fn_));
    registerPhaseNode(phase_, task_handle, task_desc);
    return task_handle;
  }

  template <class Fn>
    requires std::invocable<Fn&> || std::invocable<Fn&, const CancelToken&>
  auto systemTask(const FrameSystemDesc& system_desc_,
                  std::initializer_list<ResourceAccessDesc> accesses_,
                  Fn&& fn_) -> TaskHandle
  {
    auto task_desc = Detail::normalizeFrameSystemTaskDesc(system_desc_);
    auto task_handle = plan_.task(task_desc, std::forward<Fn>(fn_));
    registerPhaseNode(system_desc_.phase, task_handle, task_desc);
    registerResourceAccesses(task_handle, accesses_);
    return task_handle;
  }

  auto gate(FramePhase phase_, const TaskDesc& desc_) -> GateHandle
  {
    auto gate_desc = Detail::normalizeFrameTaskDesc(phase_, desc_);
    auto gate_handle = plan_.gate(gate_desc);
    registerPhaseNode(phase_, gate_handle, Detail::normalizeGateDesc(gate_desc));
    return gate_handle;
  }

  auto mainThreadGate(FramePhase phase_, const TaskDesc& desc_) -> GateHandle
  {
    auto gate_desc = Detail::normalizeFrameTaskDesc(phase_, desc_);
    auto gate_handle = plan_.mainThreadGate(gate_desc);
    registerPhaseNode(phase_, gate_handle, Detail::normalizeMainThreadGateDesc(gate_desc));
    return gate_handle;
  }

  auto phaseBarrier(FramePhase phase_, const TaskDesc& desc_) -> GateHandle
  {
    auto barrier_desc = Detail::normalizeGateDesc(Detail::normalizeFrameTaskDesc(phase_, desc_));
    auto barrier_handle = plan_.gate(barrier_desc);
    auto& phase_state = activatePhase(phase_);

    if (!phase_state.tail_nodes.empty()) {
      for (const auto& tail_node : phase_state.tail_nodes) {
        precedeStoredNode(tail_node, barrier_handle);
      }
    }
    else {
      wireBarrierSources(phase_state, barrier_handle);
    }

    phase_state.tail_nodes.clear();
    appendUniqueNode(phase_state.tail_nodes, barrier_handle);
    registerProfileNode(phase_, barrier_handle, barrier_desc);
    return barrier_handle;
  }

  template <std::integral Index, class Fn>
    requires std::invocable<Fn&, Index> || std::invocable<Fn&, Index, const CancelToken&>
  auto parallelFor(
    FramePhase phase_, Index first_, Index last_, Index step_, const TaskDesc& desc_, Fn&& fn_)
    -> TaskHandle
  {
    auto task_desc = Detail::normalizeFrameTaskDesc(phase_, desc_);
    auto task_handle = plan_.parallelFor(task_desc, first_, last_, step_, std::forward<Fn>(fn_));
    registerPhaseNode(phase_, task_handle, task_desc);
    return task_handle;
  }

  template <std::integral Index, class Fn>
    requires std::invocable<Fn&, Index> || std::invocable<Fn&, Index, const CancelToken&>
  auto parallelForSystem(const FrameSystemDesc& system_desc_,
                         std::initializer_list<ResourceAccessDesc> accesses_,
                         Index first_,
                         Index last_,
                         Index step_,
                         Fn&& fn_) -> TaskHandle
  {
    auto task_desc = Detail::normalizeFrameSystemTaskDesc(system_desc_);
    auto task_handle = plan_.parallelFor(task_desc, first_, last_, step_, std::forward<Fn>(fn_));
    registerPhaseNode(system_desc_.phase, task_handle, task_desc);
    registerResourceAccesses(task_handle, accesses_);
    return task_handle;
  }

  void precede(TaskHandle from_, TaskHandle to_)
  {
    plan_.precede(from_, to_);
    recordEdgeProfile(from_.node_id_, to_.node_id_);
  }

  void precede(TaskHandle from_, GateHandle to_)
  {
    plan_.precede(from_, to_);
    recordEdgeProfile(from_.node_id_, to_.node_id_);
  }

  void precede(GateHandle from_, TaskHandle to_)
  {
    plan_.precede(from_, to_);
    recordEdgeProfile(from_.node_id_, to_.node_id_);
  }

  void precede(GateHandle from_, GateHandle to_)
  {
    plan_.precede(from_, to_);
    recordEdgeProfile(from_.node_id_, to_.node_id_);
  }

  void setAutoPhaseBarriers(bool enabled_) noexcept
  {
    auto_phase_barriers_ = enabled_;
  }

  [[nodiscard]] auto autoPhaseBarriersEnabled() const noexcept -> bool
  {
    return auto_phase_barriers_;
  }

  void reserveResourceStates(std::size_t count_)
  {
    resource_states_.reserve(count_);
  }

  [[nodiscard]] auto scratchBytes() const noexcept -> std::size_t
  {
    return memory_config_.scratch_bytes;
  }

  void clear()
  {
    plan_.clear();
    resetMetadata();
  }

  [[nodiscard]] auto buildProfileReport() const -> FrameProfileReport
  {
    return makeProfileReport();
  }

  [[nodiscard]] auto compile() && -> BasicCompiledFramePlan<Backend>
  {
    auto profile_report = makeProfileReport();
    auto runtime_collector = std::make_shared<Detail::FrameRuntimeCollector>();
    resetMetadata();
    return BasicCompiledFramePlan<Backend>{
      std::move(plan_), std::move(profile_report), std::move(runtime_collector)};
  }

private:
  using PhaseNode = std::variant<TaskHandle, GateHandle>;
  using PhaseNodeVector = std::pmr::vector<PhaseNode>;

  struct PhaseState final
  {
    explicit PhaseState(std::pmr::memory_resource* memory_resource_)
        : barrier_sources(memory_resource_), tail_nodes(memory_resource_)
    {
    }

    bool active{false};
    PhaseNodeVector barrier_sources;
    PhaseNodeVector tail_nodes;
  };

  struct ResourceState final
  {
    explicit ResourceState(std::uint64_t resource_id_, std::pmr::memory_resource* memory_resource_)
        : resource_id(resource_id_), readers(memory_resource_)
    {
    }

    std::uint64_t resource_id{0};
    bool has_writer{false};
    PhaseNode writer{};
    PhaseNodeVector readers;
  };

  struct ProfileNodeRecord final
  {
    std::uint32_t node_id{0};
    FramePhase phase{FramePhase::INPUT};
    TaskDesc desc{};
  };

  struct ProfileEdgeRecord final
  {
    std::uint32_t from_node_id{0};
    std::uint32_t to_node_id{0};
  };

  static constexpr auto kFramePhaseCount =
    static_cast<std::size_t>(FramePhase::END_FRAME) + std::size_t{1};
  static constexpr auto kExecutionLaneCount =
    static_cast<std::size_t>(ExecutionLane::SERVICE) + std::size_t{1};

  [[nodiscard]] static constexpr auto phaseIndex(FramePhase phase_) noexcept -> std::size_t
  {
    return static_cast<std::size_t>(phase_);
  }

  void initializePhaseStates()
  {
    phase_states_.reserve(kFramePhaseCount);
    for (std::size_t phase_index = 0; phase_index < kFramePhaseCount; ++phase_index) {
      phase_states_.emplace_back(scratch_arena_->resource());
    }
  }

  void resetMetadata()
  {
    scratch_arena_->reset();
    phase_states_.clear();
    resource_states_.clear();
    profile_nodes_.clear();
    profile_edges_.clear();
    initializePhaseStates();
  }

  [[nodiscard]] static auto nodeId(const TaskHandle& handle_) noexcept -> std::uint32_t
  {
    return handle_.node_id_;
  }

  [[nodiscard]] static auto nodeId(const GateHandle& handle_) noexcept -> std::uint32_t
  {
    return handle_.node_id_;
  }

  [[nodiscard]] static auto nodeId(const PhaseNode& node_) noexcept -> std::uint32_t
  {
    return std::visit(
      [](const auto& handle_) {
        return nodeId(handle_);
      },
      node_);
  }

  template <class Handle>
  void registerProfileNode(FramePhase phase_, Handle handle_, const TaskDesc& desc_)
  {
    const auto handle_id = nodeId(handle_);
    auto existing_node =
      std::find_if(profile_nodes_.begin(), profile_nodes_.end(), [handle_id](const auto& node_) {
        return node_.node_id == handle_id;
      });
    if (existing_node == profile_nodes_.end()) {
      profile_nodes_.push_back(ProfileNodeRecord{
        .node_id = handle_id,
        .phase = phase_,
        .desc = desc_,
      });
    }
  }

  void recordEdgeProfile(std::uint32_t from_node_id_, std::uint32_t to_node_id_)
  {
    auto existing_edge =
      std::find_if(profile_edges_.begin(),
                   profile_edges_.end(),
                   [from_node_id_, to_node_id_](const auto& edge_) {
                     return edge_.from_node_id == from_node_id_ && edge_.to_node_id == to_node_id_;
                   });
    if (existing_edge == profile_edges_.end()) {
      profile_edges_.push_back(ProfileEdgeRecord{
        .from_node_id = from_node_id_,
        .to_node_id = to_node_id_,
      });
    }
  }

  [[nodiscard]] auto makeProfileReport() const -> FrameProfileReport
  {
    FrameProfileReport report;
    report.node_count = profile_nodes_.size();
    report.edge_count = profile_edges_.size();

    struct PhaseAccumulator final
    {
      std::uint32_t task_count{0};
      std::uint32_t gate_count{0};
      std::uint64_t estimated_cost_ns{0};
      bool active{false};
    };

    struct LaneAccumulator final
    {
      std::uint32_t task_count{0};
      std::uint64_t estimated_cost_ns{0};
      bool active{false};
    };

    auto phase_accumulators = std::array<PhaseAccumulator, kFramePhaseCount>{};
    auto lane_accumulators = std::array<LaneAccumulator, kExecutionLaneCount>{};

    for (const auto& profile_node : profile_nodes_) {
      auto& phase_accumulator = phase_accumulators[phaseIndex(profile_node.phase)];
      phase_accumulator.active = true;
      ++phase_accumulator.task_count;
      phase_accumulator.estimated_cost_ns += profile_node.desc.estimated_cost_ns;
      if (isGateTask(profile_node.desc)) {
        ++phase_accumulator.gate_count;
      }

      const auto lane_index = static_cast<std::size_t>(profile_node.desc.lane);
      auto& lane_accumulator = lane_accumulators[lane_index];
      lane_accumulator.active = true;
      ++lane_accumulator.task_count;
      lane_accumulator.estimated_cost_ns += profile_node.desc.estimated_cost_ns;
    }

    for (std::size_t phase_index = 0; phase_index < kFramePhaseCount; ++phase_index) {
      const auto& phase_accumulator = phase_accumulators[phase_index];
      if (!phase_accumulator.active) {
        continue;
      }

      report.phase_profiles.push_back(FramePhaseProfile{
        .phase = static_cast<FramePhase>(phase_index),
        .task_count = phase_accumulator.task_count,
        .gate_count = phase_accumulator.gate_count,
        .estimated_cost_ns = phase_accumulator.estimated_cost_ns,
      });
    }

    for (std::size_t lane_index = 0; lane_index < kExecutionLaneCount; ++lane_index) {
      const auto& lane_accumulator = lane_accumulators[lane_index];
      if (!lane_accumulator.active) {
        continue;
      }

      report.lane_profiles.push_back(ExecutionLaneProfile{
        .lane = static_cast<ExecutionLane>(lane_index),
        .task_count = lane_accumulator.task_count,
        .estimated_cost_ns = lane_accumulator.estimated_cost_ns,
      });
    }

    if (profile_nodes_.empty()) {
      return report;
    }

    const auto max_node_id = std::max_element(profile_nodes_.begin(),
                                              profile_nodes_.end(),
                                              [](const auto& lhs_, const auto& rhs_) {
                                                return lhs_.node_id < rhs_.node_id;
                                              })
                               ->node_id;

    auto node_index_by_id = std::vector<int>(static_cast<std::size_t>(max_node_id) + 1u, -1);
    for (std::size_t node_index = 0; node_index < profile_nodes_.size(); ++node_index) {
      node_index_by_id[profile_nodes_[node_index].node_id] = static_cast<int>(node_index);
    }

    auto adjacency = std::vector<std::vector<std::size_t>>(profile_nodes_.size());
    auto indegree = std::vector<std::uint32_t>(profile_nodes_.size(), 0);

    for (const auto& profile_edge : profile_edges_) {
      const auto from_index = node_index_by_id[profile_edge.from_node_id];
      const auto to_index = node_index_by_id[profile_edge.to_node_id];
      if (from_index < 0 || to_index < 0) {
        continue;
      }

      adjacency[static_cast<std::size_t>(from_index)].push_back(static_cast<std::size_t>(to_index));
      ++indegree[static_cast<std::size_t>(to_index)];
    }

    auto distance = std::vector<std::uint64_t>(profile_nodes_.size(), 0);
    auto predecessor = std::vector<int>(profile_nodes_.size(), -1);
    auto ready_nodes = std::vector<std::size_t>{};
    ready_nodes.reserve(profile_nodes_.size());

    for (std::size_t node_index = 0; node_index < profile_nodes_.size(); ++node_index) {
      distance[node_index] = profile_nodes_[node_index].desc.estimated_cost_ns;
      if (indegree[node_index] == 0) {
        ready_nodes.push_back(node_index);
      }
    }

    std::size_t ready_cursor = 0;
    while (ready_cursor < ready_nodes.size()) {
      const auto from_index = ready_nodes[ready_cursor++];
      for (const auto to_index : adjacency[from_index]) {
        const auto candidate_distance =
          distance[from_index] + profile_nodes_[to_index].desc.estimated_cost_ns;
        if (candidate_distance > distance[to_index]) {
          distance[to_index] = candidate_distance;
          predecessor[to_index] = static_cast<int>(from_index);
        }

        if (--indegree[to_index] == 0) {
          ready_nodes.push_back(to_index);
        }
      }
    }

    const auto end_it = std::max_element(distance.begin(), distance.end());
    if (end_it == distance.end()) {
      return report;
    }

    auto path_indices = std::vector<std::size_t>{};
    for (auto current_index = static_cast<int>(std::distance(distance.begin(), end_it));
         current_index >= 0;
         current_index = predecessor[static_cast<std::size_t>(current_index)]) {
      path_indices.push_back(static_cast<std::size_t>(current_index));
    }
    std::reverse(path_indices.begin(), path_indices.end());

    report.critical_path.total_estimated_cost_ns = *end_it;
    report.critical_path.nodes.reserve(path_indices.size());
    for (const auto node_index : path_indices) {
      const auto& profile_node = profile_nodes_[node_index];
      report.critical_path.nodes.push_back(CriticalPathNode{
        .node_id = profile_node.node_id,
        .name = std::string(profile_node.desc.name),
        .phase = profile_node.phase,
        .lane = profile_node.desc.lane,
        .task_kind = profile_node.desc.task_kind,
        .estimated_cost_ns = profile_node.desc.estimated_cost_ns,
      });
    }

    return report;
  }

  template <class Handle> static void appendUniqueNode(PhaseNodeVector& nodes_, Handle handle_)
  {
    const auto handle_id = nodeId(handle_);
    auto duplicate = std::find_if(nodes_.begin(), nodes_.end(), [handle_id](const auto& node_) {
      return nodeId(node_) == handle_id;
    });
    if (duplicate == nodes_.end()) {
      nodes_.emplace_back(handle_);
    }
  }

  template <class Handle>
  static void appendUniqueNode(std::vector<PhaseNode>& nodes_, Handle handle_)
  {
    const auto handle_id = nodeId(handle_);
    auto duplicate = std::find_if(nodes_.begin(), nodes_.end(), [handle_id](const auto& node_) {
      return nodeId(node_) == handle_id;
    });
    if (duplicate == nodes_.end()) {
      nodes_.emplace_back(handle_);
    }
  }

  [[nodiscard]] auto collectPreviousPhaseTails(std::size_t phase_index_) const
    -> std::vector<PhaseNode>
  {
    if (phase_index_ == 0) {
      return {};
    }

    for (auto previous_index = phase_index_; previous_index > 0; --previous_index) {
      const auto& previous_state = phase_states_[previous_index - 1];
      if (previous_state.active && !previous_state.tail_nodes.empty()) {
        return {previous_state.tail_nodes.begin(), previous_state.tail_nodes.end()};
      }
    }

    return {};
  }

  auto activatePhase(FramePhase phase_) -> PhaseState&
  {
    auto& phase_state = phase_states_[phaseIndex(phase_)];

    if (!phase_state.active) {
      phase_state.active = true;
      auto previous_tails = collectPreviousPhaseTails(phaseIndex(phase_));
      for (const auto& node : previous_tails) {
        appendUniqueNode(phase_state.barrier_sources, node);
      }
    }

    return phase_state;
  }

  auto findOrCreateResourceState(std::uint64_t resource_id_) -> ResourceState&
  {
    auto resource_it =
      std::lower_bound(resource_states_.begin(),
                       resource_states_.end(),
                       resource_id_,
                       [](const ResourceState& state_, std::uint64_t resource_id_) {
                         return state_.resource_id < resource_id_;
                       });

    if (resource_it == resource_states_.end() || resource_it->resource_id != resource_id_) {
      resource_it = resource_states_.insert(
        resource_it, ResourceState{resource_id_, scratch_arena_->resource()});
    }

    return *resource_it;
  }

  template <class ToHandle> void wireBarrierSources(const PhaseState& phase_state_, ToHandle to_)
  {
    if (!auto_phase_barriers_) {
      return;
    }

    for (const auto& source_node : phase_state_.barrier_sources) {
      precedeStoredNode(source_node, to_);
    }
  }

  template <class ToHandle>
  void registerPhaseNode(FramePhase phase_, ToHandle node_, const TaskDesc& desc_)
  {
    auto& phase_state = activatePhase(phase_);
    wireBarrierSources(phase_state, node_);
    appendUniqueNode(phase_state.tail_nodes, node_);
    registerProfileNode(phase_, node_, desc_);
  }

  template <class ToHandle> void precedeStoredNode(const PhaseNode& from_, ToHandle to_)
  {
    std::visit(
      [this, to_](const auto& from_handle_) {
        plan_.precede(from_handle_, to_);
        recordEdgeProfile(nodeId(from_handle_), nodeId(to_));
      },
      from_);
  }

  template <class Handle>
  void registerResourceAccesses(Handle node_, std::initializer_list<ResourceAccessDesc> accesses_)
  {
    std::vector<PhaseNode> prerequisites;
    prerequisites.reserve(accesses_.size() * 2u);

    for (const auto& access_desc : accesses_) {
      auto& resource_state = findOrCreateResourceState(access_desc.resource_id);

      if (resource_state.has_writer) {
        appendUniqueNode(prerequisites, resource_state.writer);
      }

      if (isWriteAccess(access_desc.mode)) {
        for (const auto& reader_node : resource_state.readers) {
          appendUniqueNode(prerequisites, reader_node);
        }
      }
    }

    for (const auto& prerequisite : prerequisites) {
      precedeStoredNode(prerequisite, node_);
    }

    for (const auto& access_desc : accesses_) {
      auto& resource_state = findOrCreateResourceState(access_desc.resource_id);

      if (isWriteAccess(access_desc.mode)) {
        resource_state.has_writer = true;
        resource_state.writer = node_;
        resource_state.readers.clear();
      }
      else {
        appendUniqueNode(resource_state.readers, node_);
      }
    }
  }

  PlanType plan_;
  FrameMemoryConfig memory_config_{};
  std::shared_ptr<Detail::FrameScratchArena> scratch_arena_{};
  std::pmr::vector<PhaseState> phase_states_;
  std::pmr::vector<ResourceState> resource_states_;
  std::pmr::vector<ProfileNodeRecord> profile_nodes_;
  std::pmr::vector<ProfileEdgeRecord> profile_edges_;
  bool auto_phase_barriers_{true};
};

template <Detail::BackendAdapter Backend> class BasicCenter final
{
public:
  using BackendType = Backend;
  using PlanType = BasicPlan<Backend>;
  using FramePlanType = BasicFramePlan<Backend>;
  using FrameInstanceType = BasicFrameInstance<Backend>;
  using RunHandle = BasicRunHandle<Backend>;

  explicit BasicCenter(ExecutorConfig config_ = {}, FrameMemoryConfig frame_memory_config_ = {})
      : config_(config_), frame_memory_config_(frame_memory_config_),
        native_(Backend::makeEngine(config_))
  {
    if constexpr (std::same_as<Backend, Detail::TaskflowBackend>) {
      runtime_observer_ =
        native_.template make_observer<Detail::TaskflowRuntimeObserver>(&active_runtime_collector_);
    }
  }

  [[nodiscard]] auto makePlan() const -> PlanType
  {
    return PlanType{trace_hooks_};
  }

  [[nodiscard]] auto makeFramePlan() const -> FramePlanType
  {
    return FramePlanType{trace_hooks_, frame_memory_config_};
  }

  auto dispatch(PlanType& plan_) -> RunHandle
  {
    return dispatch(plan_, CancelToken{});
  }

  auto dispatch(PlanType& plan_, const CancelToken& cancel_token_) -> RunHandle
  {
    plan_.runtime_state_->cancel_state = cancel_token_.stopState();
    plan_.runPreDispatchHooks();
    Detail::emitDispatchEvent(
      plan_.runtime_state_->trace_hooks, DispatchEventType::BEGIN, config_.workers);
    return RunHandle{Backend::dispatch(native_, plan_.native_),
                     plan_.runtime_state_->trace_hooks,
                     config_.workers,
                     &clearActiveRuntimeCollector,
                     this};
  }

  auto dispatchFrame(FrameInstanceType& frame_) -> RunHandle
  {
    return dispatchFrame(frame_, CancelToken{});
  }

  auto dispatchFrame(FrameInstanceType& frame_, const CancelToken& cancel_token_) -> RunHandle
  {
    if (frame_.runtime_collector_ != nullptr) {
      frame_.runtime_collector_->setWorkerCount(static_cast<std::size_t>(config_.workers));
      frame_.runtime_collector_->prepareDispatch(*frame_.plan_.runtime_state_, trace_hooks_);
      active_runtime_collector_.store(frame_.runtime_collector_.get(), std::memory_order_relaxed);
    }
    return dispatch(frame_.plan_, cancel_token_);
  }

  void waitIdle()
  {
    Backend::waitIdle(native_);
  }

  void setTraceHooks(const TraceHooks& trace_hooks_)
  {
    this->trace_hooks_ = trace_hooks_;
  }

  void setFrameMemoryConfig(const FrameMemoryConfig& frame_memory_config_)
  {
    this->frame_memory_config_ = frame_memory_config_;
  }

  [[nodiscard]] auto frameMemoryConfig() const noexcept -> const FrameMemoryConfig&
  {
    return frame_memory_config_;
  }

  [[nodiscard]] auto workerCount() const noexcept -> std::uint32_t
  {
    return config_.workers;
  }

private:
  static void clearActiveRuntimeCollector(void* user_data_)
  {
    auto* center = static_cast<BasicCenter*>(user_data_);
    center->active_runtime_collector_.store(nullptr, std::memory_order_relaxed);
  }

  ExecutorConfig config_{};
  FrameMemoryConfig frame_memory_config_{};
  TraceHooks trace_hooks_{};
  typename Backend::EngineType native_;
  std::atomic<Detail::FrameRuntimeCollector*> active_runtime_collector_{nullptr};
  std::shared_ptr<Detail::TaskflowRuntimeObserver> runtime_observer_{};
};

using DefaultBackend = Detail::TaskflowBackend;
using Center = BasicCenter<DefaultBackend>;
using Plan = BasicPlan<DefaultBackend>;
using DynamicContext = BasicDynamicContext<DefaultBackend>;
using FramePlan = BasicFramePlan<DefaultBackend>;
using CompiledFramePlan = BasicCompiledFramePlan<DefaultBackend>;
using FrameInstance = BasicFrameInstance<DefaultBackend>;
using TaskHandle = BasicTaskHandle<DefaultBackend>;
using GateHandle = BasicGateHandle<DefaultBackend>;
using RunHandle = BasicRunHandle<DefaultBackend>;

} // namespace ThreadCenter

}
