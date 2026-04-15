#pragma once

#include "thread_center/common.hpp"

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
    Detail::emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::SUBMITTED);
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
    Detail::emitTaskEvent(runtime_state_->trace_hooks, condition_desc, TaskEventType::SUBMITTED);
    return task_handle;
  }

  auto gate(const TaskDesc& desc_) -> GateHandle
  {
    auto gate_desc = Detail::normalizeGateDesc(desc_);
    auto gate_handle =
      GateHandle{Backend::emplace(
                   subflow_, gate_desc, Detail::makeTaskInvoker(gate_desc, runtime_state_, [] {})),
                 allocateNodeId()};
    Detail::emitTaskEvent(runtime_state_->trace_hooks, gate_desc, TaskEventType::SUBMITTED);
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
    Detail::emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::SUBMITTED);
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
    Detail::emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::SUBMITTED);
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
    Detail::emitTaskEvent(runtime_state_->trace_hooks, condition_desc, TaskEventType::SUBMITTED);
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
    Detail::emitTaskEvent(runtime_state_->trace_hooks, dynamic_desc, TaskEventType::SUBMITTED);
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
      Detail::emitTaskEvent(runtime_state_->trace_hooks, pipeline_desc, TaskEventType::SUBMITTED);
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
    Detail::emitTaskEvent(runtime_state_->trace_hooks, gate_desc, TaskEventType::SUBMITTED);
    return gate_handle;
  }

  auto mainThreadGate(const TaskDesc& desc_) -> GateHandle
  {
    auto gate_desc = Detail::normalizeMainThreadGateDesc(desc_);
    auto gate_handle =
      GateHandle{Backend::emplace(
                   native_, gate_desc, Detail::makeTaskInvoker(gate_desc, runtime_state_, [] {})),
                 allocateNodeId()};
    Detail::emitTaskEvent(runtime_state_->trace_hooks, gate_desc, TaskEventType::SUBMITTED);
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
    Detail::emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::SUBMITTED);
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
