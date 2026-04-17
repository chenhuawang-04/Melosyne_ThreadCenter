module;

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <future>
#include <functional>
#include <initializer_list>
#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <taskflow/algorithm/for_each.hpp>
#include <taskflow/algorithm/pipeline.hpp>
#include <taskflow/taskflow.hpp>

export module Center.Thread.Common;

export {
namespace ThreadCenter
{

struct TaskDesc;

namespace Detail
{
struct CancelState;
struct PlanRuntimeState;
[[nodiscard]] auto parallelForBatchSize(const TaskDesc& desc_) noexcept -> std::size_t;
} // namespace Detail

class CancelToken;

} // namespace ThreadCenter

namespace ThreadCenter
{

struct ExecutorConfig;
struct TaskDesc;

namespace Detail
{

struct TaskflowBackend final
{
  using EngineType = tf::Executor;
  using GraphType = tf::Taskflow;
  using NodeType = tf::Task;
  using RunHandleType = tf::Future<void>;
  using SubflowType = tf::Subflow;

  static auto makeEngine(const ExecutorConfig& config_) -> EngineType;
  static auto makeGraph() -> GraphType;

  template <class Fn>
  static auto emplace(GraphType& graph_, const TaskDesc& desc_, Fn&& fn_) -> NodeType
  {
    auto task = graph_.emplace(std::forward<Fn>(fn_));
    decorate(task, desc_);
    return task;
  }

  template <class Index, class Fn>
  static auto parallelFor(
    GraphType& graph_, const TaskDesc& desc_, Index first_, Index last_, Index step_, Fn&& fn_)
    -> NodeType
  {
    auto task = graph_.for_each_index(first_, last_, step_, std::forward<Fn>(fn_));
    decorate(task, desc_);
    return task;
  }

  template <class Fn>
  static auto emplace(SubflowType& subflow_, const TaskDesc& desc_, Fn&& fn_) -> NodeType
  {
    auto task = subflow_.emplace(std::forward<Fn>(fn_));
    decorate(task, desc_);
    return task;
  }

  template <class Index, class Fn>
  static auto parallelFor(
    SubflowType& subflow_, const TaskDesc& desc_, Index first_, Index last_, Index step_, Fn&& fn_)
    -> NodeType
  {
    auto task = subflow_.for_each_index(first_, last_, step_, std::forward<Fn>(fn_));
    decorate(task, desc_);
    return task;
  }

  static void join(SubflowType& subflow_)
  {
    subflow_.join();
  }

  static void depend(NodeType from_, NodeType to_);
  static auto dispatch(EngineType& engine_, GraphType& graph_) -> RunHandleType;
  static void wait(RunHandleType& run_handle_);
  static void waitIdle(EngineType& engine_);
  static void clear(GraphType& graph_);
  static void decorate(NodeType task_, const TaskDesc& desc_);
};

} // namespace Detail
} // namespace ThreadCenter

namespace ThreadCenter
{

enum class ScheduleDomain : std::uint8_t
{
  FRAME,
  GAMEPLAY,
  ANIMATION,
  PHYSICS,
  RENDERING,
  STREAMING,
  BACKGROUND
};

enum class FramePhase : std::uint8_t
{
  INPUT,
  EARLY_UPDATE,
  GAMEPLAY,
  PHYSICS,
  ANIMATION,
  AI,
  CULLING,
  RENDER_PREP,
  RENDER_SUBMIT,
  STREAMING_FINALIZE,
  END_FRAME
};

enum class TaskPriority : std::uint8_t
{
  CRITICAL,
  HIGH,
  NORMAL,
  LOW
};

enum class ResourceAccessMode : std::uint8_t
{
  READ,
  WRITE,
  READ_WRITE
};

enum class SystemGroup : std::uint8_t
{
  ENGINE,
  GAMEPLAY,
  PHYSICS,
  ANIMATION,
  AI,
  RENDER,
  STREAMING,
  SERVICE
};

enum class TaskKind : std::uint8_t
{
  COMPUTE,
  PARALLEL_FOR,
  PARALLEL_REDUCE,
  DYNAMIC,
  CONDITION,
  PIPELINE,
  EXTERNAL,
  GATE,
  DAEMON
};

enum class ExecutionLane : std::uint8_t
{
  FRAME_CRITICAL,
  FRAME_NORMAL,
  BACKGROUND,
  STREAMING,
  BLOCKING,
  RENDER_ASSIST,
  SERVICE
};

enum class BudgetClass : std::uint8_t
{
  REALTIME,
  FRAME_BOUND,
  ELASTIC,
  BACKGROUND
};

enum class OversubscribePolicy : std::uint8_t
{
  INHERIT,
  NEVER,
  ALLOW_FOR_BLOCKING,
  ALWAYS
};

enum class CancelBehavior : std::uint8_t
{
  INHERIT,
  COOPERATIVE,
  NON_CANCELLABLE
};

enum class TaskFlags : std::uint32_t
{
  NONE = 0,
  LONG_RUNNING = 1u << 0,
  NEVER_INLINE = 1u << 1,
  MAIN_THREAD_GATE = 1u << 2,
  BLOCKING = 1u << 3,
  DETACHED = 1u << 4,
  BACKEND_NAME = 1u << 5
};

[[nodiscard]] constexpr auto operator|(TaskFlags lhs_, TaskFlags rhs_) noexcept -> TaskFlags
{
  using ValueType = std::underlying_type_t<TaskFlags>;
  return static_cast<TaskFlags>(static_cast<ValueType>(lhs_) | static_cast<ValueType>(rhs_));
}

constexpr auto operator|=(TaskFlags& lhs_, TaskFlags rhs_) noexcept -> TaskFlags&
{
  lhs_ = lhs_ | rhs_;
  return lhs_;
}

[[nodiscard]] constexpr auto hasFlag(TaskFlags value_, TaskFlags flag_) noexcept -> bool
{
  using ValueType = std::underlying_type_t<TaskFlags>;
  return (static_cast<ValueType>(value_) & static_cast<ValueType>(flag_)) != 0;
}

[[nodiscard]] constexpr auto isBlockingLane(ExecutionLane lane_) noexcept -> bool
{
  return lane_ == ExecutionLane::BLOCKING;
}

[[nodiscard]] constexpr auto isRealtimeBudget(BudgetClass budget_class_) noexcept -> bool
{
  return budget_class_ == BudgetClass::REALTIME;
}

inline constexpr std::uint8_t kInvalidFramePhaseTag = 0xFFu;

[[nodiscard]] constexpr auto hasFramePhaseTag(std::uint8_t frame_phase_tag_) noexcept -> bool
{
  return frame_phase_tag_ != kInvalidFramePhaseTag;
}

[[nodiscard]] constexpr auto framePhaseFromTag(std::uint8_t frame_phase_tag_) noexcept -> FramePhase
{
  return static_cast<FramePhase>(frame_phase_tag_);
}

struct ResourceAccessDesc final
{
  std::uint64_t resource_id{0};
  ResourceAccessMode mode{ResourceAccessMode::READ};
};

[[nodiscard]] constexpr auto isWriteAccess(ResourceAccessMode mode_) noexcept -> bool
{
  return mode_ == ResourceAccessMode::WRITE || mode_ == ResourceAccessMode::READ_WRITE;
}

struct TaskDesc final
{
  std::string_view name{};
  ScheduleDomain domain{ScheduleDomain::FRAME};
  TaskKind task_kind{TaskKind::COMPUTE};
  ExecutionLane lane{ExecutionLane::FRAME_NORMAL};
  TaskPriority priority{TaskPriority::NORMAL};
  BudgetClass budget_class{BudgetClass::FRAME_BOUND};
  TaskFlags flags{TaskFlags::NONE};
  OversubscribePolicy oversubscribe_policy{OversubscribePolicy::INHERIT};
  CancelBehavior cancel_behavior{CancelBehavior::COOPERATIVE};
  std::uint32_t affinity_mask{0};
  std::uint32_t debug_tag{0};
  std::uint32_t trace_color_abgr{0};
  std::uint16_t stack_hint_kb{0};
  std::uint16_t max_concurrency{0};
  std::uint32_t min_grain_size{1};
  std::uint64_t estimated_cost_ns{0};
  std::uint8_t frame_phase_tag{kInvalidFramePhaseTag};
};

namespace Detail
{
[[nodiscard]] inline auto parallelForBatchSize(const TaskDesc& desc_) noexcept -> std::size_t
{
  return std::max<std::size_t>(std::size_t{1}, desc_.min_grain_size);
}
} // namespace Detail

struct FrameSystemDesc final
{
  std::string_view name{};
  FramePhase phase{FramePhase::GAMEPLAY};
  SystemGroup system_group{SystemGroup::GAMEPLAY};
  TaskDesc task_desc{};
};

[[nodiscard]] constexpr auto isBlockingTask(const TaskDesc& desc_) noexcept -> bool
{
  return isBlockingLane(desc_.lane) || hasFlag(desc_.flags, TaskFlags::BLOCKING) ||
         hasFlag(desc_.flags, TaskFlags::LONG_RUNNING);
}

[[nodiscard]] constexpr auto isGateTask(const TaskDesc& desc_) noexcept -> bool
{
  return desc_.task_kind == TaskKind::GATE || hasFlag(desc_.flags, TaskFlags::MAIN_THREAD_GATE);
}

struct ExecutorConfig final
{
  std::uint32_t workers{std::max(1u, std::thread::hardware_concurrency())};
  bool allow_worker_binding{false};
};

struct FrameMemoryConfig final
{
  std::size_t scratch_bytes{64u * 1024u};
  std::pmr::memory_resource* upstream_resource{std::pmr::get_default_resource()};
};

enum class TaskEventType : std::uint8_t
{
  SUBMITTED,
  STARTED,
  FINISHED,
  CANCELED
};

enum class DispatchEventType : std::uint8_t
{
  BEGIN,
  END
};

enum class PipelineEventType : std::uint8_t
{
  STAGE_BEGIN,
  STAGE_END,
  STOP_REQUESTED
};

enum class PipelineStageKind : std::uint8_t
{
  SERIAL,
  PARALLEL
};

class PipelineTokenContext final
{
public:
  using StopCallback = void (*)(void* user_data_);

  PipelineTokenContext() = default;
  PipelineTokenContext(std::size_t token_index_,
                       std::size_t stage_index_,
                       const std::shared_ptr<std::atomic_bool>& stop_requested_,
                       StopCallback stop_callback_ = nullptr,
                       void* stop_callback_user_data_ = nullptr) noexcept
      : token_index_(token_index_), stage_index_(stage_index_), stop_requested_(stop_requested_),
        stop_callback_(stop_callback_), stop_callback_user_data_(stop_callback_user_data_)
  {
  }

  [[nodiscard]] auto tokenIndex() const noexcept -> std::size_t
  {
    return token_index_;
  }

  [[nodiscard]] auto stageIndex() const noexcept -> std::size_t
  {
    return stage_index_;
  }

  void stop() const noexcept
  {
    if (stop_requested_) {
      stop_requested_->store(true, std::memory_order_relaxed);
    }

    if (stop_callback_ != nullptr) {
      stop_callback_(stop_callback_user_data_);
    }
  }

  [[nodiscard]] auto stopRequested() const noexcept -> bool
  {
    return stop_requested_ != nullptr && stop_requested_->load(std::memory_order_relaxed);
  }

private:
  std::size_t token_index_{0};
  std::size_t stage_index_{0};
  std::shared_ptr<std::atomic_bool> stop_requested_{};
  StopCallback stop_callback_{nullptr};
  void* stop_callback_user_data_{nullptr};
};

struct PipelineStageDesc final
{
  std::string_view name{};
  PipelineStageKind kind{PipelineStageKind::SERIAL};
  TaskDesc task_desc{};
};

class PipelineBuilder final
{
public:
  using StageCallback = std::function<void(PipelineTokenContext&, const CancelToken&)>;

  struct StageSpec final
  {
    PipelineStageDesc desc{};
    StageCallback callback{};
  };

  template <class Fn> void serialStage(const PipelineStageDesc& desc_, Fn&& fn_)
  {
    addStage(PipelineStageKind::SERIAL, desc_, std::forward<Fn>(fn_));
  }

  template <class Fn> void parallelStage(const PipelineStageDesc& desc_, Fn&& fn_)
  {
    addStage(PipelineStageKind::PARALLEL, desc_, std::forward<Fn>(fn_));
  }

  [[nodiscard]] auto releaseStages() && -> std::vector<StageSpec>
  {
    return std::move(stage_specs_);
  }

private:
  template <class Fn>
  void addStage(PipelineStageKind kind_, const PipelineStageDesc& desc_, Fn&& fn_)
  {
    auto stage_desc = desc_;
    stage_desc.kind = kind_;
    stage_specs_.push_back(StageSpec{
      .desc = stage_desc,
      .callback =
        [fn = std::forward<Fn>(fn_)](PipelineTokenContext& token_context_,
                                     const CancelToken& cancel_token_) mutable {
          if constexpr (std::invocable<Fn&, PipelineTokenContext&, const CancelToken&>) {
            fn(token_context_, cancel_token_);
          }
          else {
            fn(token_context_);
          }
        },
    });
  }

  std::vector<StageSpec> stage_specs_{};
};

struct TaskEvent final
{
  TaskEventType type{};
  std::string_view name{};
  TaskKind task_kind{TaskKind::COMPUTE};
  ExecutionLane lane{ExecutionLane::FRAME_NORMAL};
  TaskPriority priority{TaskPriority::NORMAL};
  BudgetClass budget_class{BudgetClass::FRAME_BOUND};
  TaskFlags flags{TaskFlags::NONE};
  std::uint32_t debug_tag{0};
  std::uint64_t estimated_cost_ns{0};
  std::uint8_t frame_phase_tag{kInvalidFramePhaseTag};
};

struct DispatchEvent final
{
  DispatchEventType type{};
  std::uint32_t worker_count{0};
};

struct PipelineEvent final
{
  PipelineEventType type{};
  std::string_view pipeline_name{};
  std::string_view stage_name{};
  PipelineStageKind stage_kind{PipelineStageKind::SERIAL};
  std::size_t stage_index{0};
  std::size_t token_index{0};
};

using TaskEventCallback = void (*)(void* user_data_, const TaskEvent& event_);
using DispatchEventCallback = void (*)(void* user_data_, const DispatchEvent& event_);
using PipelineEventCallback = void (*)(void* user_data_, const PipelineEvent& event_);

struct TraceHooks final
{
  void* user_data{nullptr};
  TaskEventCallback on_task_event{nullptr};
  DispatchEventCallback on_dispatch_event{nullptr};
  PipelineEventCallback on_pipeline_event{nullptr};
};

struct FramePhaseProfile final
{
  FramePhase phase{FramePhase::INPUT};
  std::uint32_t task_count{0};
  std::uint32_t gate_count{0};
  std::uint64_t estimated_cost_ns{0};
};

struct ExecutionLaneProfile final
{
  ExecutionLane lane{ExecutionLane::FRAME_NORMAL};
  std::uint32_t task_count{0};
  std::uint64_t estimated_cost_ns{0};
};

struct CriticalPathNode final
{
  std::uint32_t node_id{0};
  std::string name{};
  FramePhase phase{FramePhase::INPUT};
  ExecutionLane lane{ExecutionLane::FRAME_NORMAL};
  TaskKind task_kind{TaskKind::COMPUTE};
  std::uint64_t estimated_cost_ns{0};
};

struct CriticalPathReport final
{
  std::uint64_t total_estimated_cost_ns{0};
  std::vector<CriticalPathNode> nodes{};
};

struct FrameProfileReport final
{
  std::size_t node_count{0};
  std::size_t edge_count{0};
  std::vector<FramePhaseProfile> phase_profiles{};
  std::vector<ExecutionLaneProfile> lane_profiles{};
  CriticalPathReport critical_path{};
};

struct PhaseRuntimeProfile final
{
  FramePhase phase{FramePhase::INPUT};
  std::uint32_t started_count{0};
  std::uint32_t finished_count{0};
  std::uint32_t canceled_count{0};
  std::uint32_t begin_event_count{0};
  std::uint32_t end_event_count{0};
  std::uint64_t last_begin_offset_ns{0};
  std::uint64_t last_end_offset_ns{0};
};

struct ExecutionLaneRuntimeProfile final
{
  ExecutionLane lane{ExecutionLane::FRAME_NORMAL};
  std::uint32_t started_count{0};
  std::uint32_t finished_count{0};
  std::uint32_t canceled_count{0};
};

struct WorkerRuntimeProfile final
{
  std::uint32_t worker_id{0};
  std::uint32_t started_count{0};
  std::uint32_t finished_count{0};
  std::uint32_t max_queue_size_observed{0};
  std::uint64_t busy_time_ns{0};
  std::uint64_t last_entry_offset_ns{0};
  std::uint64_t last_exit_offset_ns{0};
  std::uint32_t utilization_permille{0};
};

struct PipelineRuntimeProfile final
{
  std::uint32_t stage_begin_count{0};
  std::uint32_t stage_end_count{0};
  std::uint32_t stop_requested_count{0};
};

struct FrameRuntimeReport final
{
  std::uint32_t dispatch_count{0};
  std::vector<PhaseRuntimeProfile> phase_profiles{};
  std::vector<ExecutionLaneRuntimeProfile> lane_profiles{};
  std::vector<WorkerRuntimeProfile> worker_profiles{};
  PipelineRuntimeProfile pipeline_profile{};
};

class CancelToken final
{
public:
  CancelToken() = default;
  explicit CancelToken(std::shared_ptr<Detail::CancelState> state_) : state_(std::move(state_)) {}

  [[nodiscard]] auto stopPossible() const noexcept -> bool
  {
    return static_cast<bool>(state_);
  }

  [[nodiscard]] auto stopRequested() const noexcept -> bool;

  [[nodiscard]] auto stopState() const noexcept -> const std::shared_ptr<Detail::CancelState>&
  {
    return state_;
  }

private:
  std::shared_ptr<Detail::CancelState> state_{};

  friend class CancelSource;
  friend struct Detail::PlanRuntimeState;
};

class CancelSource final
{
public:
  CancelSource();

  [[nodiscard]] auto token() const -> CancelToken;
  void requestStop() const noexcept;
  void reset();

private:
  std::shared_ptr<Detail::CancelState> state_{};
};

namespace Detail
{

struct CancelState final
{
  std::atomic_bool stop_requested{false};
};

struct PlanRuntimeState final
{
  TraceHooks trace_hooks{};
  std::shared_ptr<CancelState> cancel_state{};
};

struct PreDispatchHook final
{
  void (*callback)(void* user_data_){nullptr};
  void* user_data_{nullptr};

  void run() const
  {
    if (callback != nullptr) {
      callback(user_data_);
    }
  }
};

class FrameScratchArena final
{
public:
  explicit FrameScratchArena(const FrameMemoryConfig& config_)
      : buffer_(config_.scratch_bytes),
        resource_(buffer_.data(), buffer_.size(), config_.upstream_resource)
  {
  }

  void reset()
  {
    resource_.release();
  }

  [[nodiscard]] auto resource() noexcept -> std::pmr::memory_resource*
  {
    return &resource_;
  }

private:
  std::vector<std::byte> buffer_;
  std::pmr::monotonic_buffer_resource resource_;
};

[[nodiscard]] inline auto makePlanRuntimeState(const TraceHooks& trace_hooks_)
  -> std::shared_ptr<PlanRuntimeState>
{
  auto runtime_state = std::make_shared<PlanRuntimeState>();
  runtime_state->trace_hooks = trace_hooks_;
  return runtime_state;
}

inline void
emitTaskEvent(const TraceHooks& trace_hooks_, const TaskDesc& desc_, TaskEventType type_)
{
  if (trace_hooks_.on_task_event == nullptr) {
    return;
  }

  trace_hooks_.on_task_event(trace_hooks_.user_data,
                             TaskEvent{
                               .type = type_,
                               .name = desc_.name,
                               .task_kind = desc_.task_kind,
                               .lane = desc_.lane,
                               .priority = desc_.priority,
                               .budget_class = desc_.budget_class,
                               .flags = desc_.flags,
                               .debug_tag = desc_.debug_tag,
                               .estimated_cost_ns = desc_.estimated_cost_ns,
                               .frame_phase_tag = desc_.frame_phase_tag,
                             });
}

inline void emitDispatchEvent(const TraceHooks& trace_hooks_,
                              DispatchEventType type_,
                              std::uint32_t worker_count_)
{
  if (trace_hooks_.on_dispatch_event == nullptr) {
    return;
  }

  trace_hooks_.on_dispatch_event(trace_hooks_.user_data,
                                 DispatchEvent{
                                   .type = type_,
                                   .worker_count = worker_count_,
                                 });
}

inline void emitPipelineEvent(const TraceHooks& trace_hooks_,
                              PipelineEventType type_,
                              std::string_view pipeline_name_,
                              const PipelineStageDesc& stage_desc_,
                              std::size_t stage_index_,
                              std::size_t token_index_)
{
  if (trace_hooks_.on_pipeline_event == nullptr) {
    return;
  }

  trace_hooks_.on_pipeline_event(trace_hooks_.user_data,
                                 PipelineEvent{
                                   .type = type_,
                                   .pipeline_name = pipeline_name_,
                                   .stage_name = stage_desc_.name,
                                   .stage_kind = stage_desc_.kind,
                                   .stage_index = stage_index_,
                                   .token_index = token_index_,
                                 });
}

[[nodiscard]] inline auto shouldCancel(const TaskDesc& desc_,
                                       const CancelToken& cancel_token_) noexcept -> bool
{
  return cancel_token_.stopRequested() && desc_.cancel_behavior != CancelBehavior::NON_CANCELLABLE;
}

template <class Fn> void invokeTaskBody(Fn& fn_, const CancelToken& cancel_token_)
{
  if constexpr (std::invocable<Fn&, const CancelToken&>) {
    fn_(cancel_token_);
  }
  else {
    fn_();
  }
}

template <class Index, class Fn>
void invokeIndexedTaskBody(Fn& fn_, Index index_, const CancelToken& cancel_token_)
{
  if constexpr (std::invocable<Fn&, Index, const CancelToken&>) {
    fn_(index_, cancel_token_);
  }
  else {
    fn_(index_);
  }
}

template <class Index>
[[nodiscard]] constexpr auto
isParallelForIndexInRange(Index value_, Index last_, Index step_) noexcept -> bool
{
  if constexpr (std::is_signed_v<Index>) {
    return step_ >= 0 ? value_ < last_ : value_ > last_;
  }
  else {
    static_cast<void>(step_);
    return value_ < last_;
  }
}

template <class Index>
[[nodiscard]] constexpr auto advanceParallelForIndex(Index value_, Index step_) noexcept -> Index
{
  return static_cast<Index>(value_ + step_);
}

template <class Index>
[[nodiscard]] constexpr auto makeParallelForBatchStride(Index step_,
                                                        std::size_t batch_size_) noexcept -> Index
{
  if constexpr (std::integral<Index>) {
    return static_cast<Index>(step_ * static_cast<Index>(batch_size_));
  }

  auto stride = step_;
  for (std::size_t batch_index = 1; batch_index < batch_size_; ++batch_index) {
    stride = advanceParallelForIndex(stride, step_);
  }
  return stride;
}

template <class Index>
[[nodiscard]] constexpr auto
parallelForBatchSpan(Index batch_first_, Index last_, Index step_, std::size_t batch_size_) noexcept
  -> std::size_t
{
  if (batch_size_ == 0) {
    return 0;
  }

  if constexpr (std::unsigned_integral<Index>) {
    if (step_ == 0 || batch_first_ >= last_) {
      return 0;
    }

    const auto remaining = static_cast<std::size_t>((last_ - batch_first_ + (step_ - 1)) / step_);
    return std::min(batch_size_, remaining);
  }
  else if constexpr (std::signed_integral<Index>) {
    if (step_ == 0) {
      return 0;
    }

    using UnsignedIndex = std::make_unsigned_t<Index>;

    if (step_ > 0) {
      if (batch_first_ >= last_) {
        return 0;
      }

      const auto remaining = static_cast<UnsignedIndex>(last_ - batch_first_);
      const auto step_value = static_cast<UnsignedIndex>(step_);
      const auto count = static_cast<std::size_t>((remaining + (step_value - 1)) / step_value);
      return std::min(batch_size_, count);
    }

    if (batch_first_ <= last_) {
      return 0;
    }

    const auto remaining = static_cast<UnsignedIndex>(batch_first_ - last_);
    const auto step_value = static_cast<UnsignedIndex>(-step_);
    const auto count = static_cast<std::size_t>((remaining + (step_value - 1)) / step_value);
    return std::min(batch_size_, count);
  }
  else {
    auto count = std::size_t{0};
    auto value = batch_first_;
    while (count < batch_size_ && isParallelForIndexInRange(value, last_, step_)) {
      value = advanceParallelForIndex(value, step_);
      ++count;
    }
    return count;
  }
}

[[nodiscard]] inline auto hasTaskTraceHook(const PlanRuntimeState& runtime_state_) noexcept -> bool
{
  return runtime_state_.trace_hooks.on_task_event != nullptr;
}

[[nodiscard]] inline auto canObserveCancellation(const TaskDesc& desc_,
                                                 const CancelState* cancel_state_) noexcept -> bool
{
  return desc_.cancel_behavior != CancelBehavior::NON_CANCELLABLE && cancel_state_ != nullptr;
}

[[nodiscard]] inline auto isCancellationRequested(const CancelState* cancel_state_) noexcept -> bool
{
  return cancel_state_ != nullptr &&
         cancel_state_->stop_requested.load(std::memory_order_relaxed);
}

template <class Index, class Fn>
[[nodiscard]] auto
makeBatchedIndexedTaskInvoker(const TaskDesc& desc_,
                              const std::shared_ptr<PlanRuntimeState>& runtime_state_,
                              Index last_,
                              Index step_,
                              std::size_t batch_size_,
  Fn&& fn_)
{
  if constexpr (std::invocable<Fn&, Index, const CancelToken&>) {
    return [desc_,
            runtime_state_,
            last_,
            step_,
            batch_size_,
            fn = std::forward<Fn>(fn_)](Index batch_first_) mutable {
      auto cancel_token = CancelToken{runtime_state_->cancel_state};
      const auto* cancel_state_ptr = runtime_state_->cancel_state.get();
      const auto trace_enabled = hasTaskTraceHook(*runtime_state_);
      const auto observe_cancellation = canObserveCancellation(desc_, cancel_state_ptr);

      if (!trace_enabled && !observe_cancellation) {
        const auto batch_count = parallelForBatchSpan(batch_first_, last_, step_, batch_size_);
        if (batch_count == 0) {
          return;
        }

        auto index = batch_first_;
        for (std::size_t batch_index = 0; batch_index < batch_count; ++batch_index) {
          fn(index, cancel_token);
          index = advanceParallelForIndex(index, step_);
        }
        return;
      }

      if (observe_cancellation && isCancellationRequested(cancel_state_ptr)) {
        emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::CANCELED);
        return;
      }

      emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::STARTED);
      const auto batch_count = parallelForBatchSpan(batch_first_, last_, step_, batch_size_);
      if (batch_count == 0) {
        emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::FINISHED);
        return;
      }

      auto index = batch_first_;
      for (std::size_t batch_index = 0; batch_index < batch_count; ++batch_index) {
        if (observe_cancellation && isCancellationRequested(cancel_state_ptr)) {
          break;
        }
        fn(index, cancel_token);
        index = advanceParallelForIndex(index, step_);
      }
      emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::FINISHED);
    };
  }
  else {
    return [desc_,
            runtime_state_,
            last_,
            step_,
            batch_size_,
            fn = std::forward<Fn>(fn_)](Index batch_first_) mutable {
      const auto* cancel_state_ptr = runtime_state_->cancel_state.get();
      const auto trace_enabled = hasTaskTraceHook(*runtime_state_);
      const auto observe_cancellation = canObserveCancellation(desc_, cancel_state_ptr);

      if (!trace_enabled && !observe_cancellation) {
        const auto batch_count = parallelForBatchSpan(batch_first_, last_, step_, batch_size_);
        if (batch_count == 0) {
          return;
        }

        auto index = batch_first_;
        for (std::size_t batch_index = 0; batch_index < batch_count; ++batch_index) {
          fn(index);
          index = advanceParallelForIndex(index, step_);
        }
        return;
      }

      if (observe_cancellation && isCancellationRequested(cancel_state_ptr)) {
        emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::CANCELED);
        return;
      }

      emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::STARTED);
      const auto batch_count = parallelForBatchSpan(batch_first_, last_, step_, batch_size_);
      if (batch_count == 0) {
        emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::FINISHED);
        return;
      }

      auto index = batch_first_;
      for (std::size_t batch_index = 0; batch_index < batch_count; ++batch_index) {
        if (observe_cancellation && isCancellationRequested(cancel_state_ptr)) {
          break;
        }
        fn(index);
        index = advanceParallelForIndex(index, step_);
      }
      emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::FINISHED);
    };
  }
}

template <class Fn>
[[nodiscard]] auto makeTaskInvoker(const TaskDesc& desc_,
                                   const std::shared_ptr<PlanRuntimeState>& runtime_state_,
                                   Fn&& fn_)
{
  if constexpr (std::invocable<Fn&, const CancelToken&>) {
    return [desc_,
            runtime_state_,
            fn = std::forward<Fn>(fn_)]() mutable {
      auto cancel_token = CancelToken{runtime_state_->cancel_state};
      const auto* cancel_state_ptr = runtime_state_->cancel_state.get();
      const auto trace_enabled = hasTaskTraceHook(*runtime_state_);
      const auto observe_cancellation = canObserveCancellation(desc_, cancel_state_ptr);

      if (!trace_enabled && !observe_cancellation) {
        fn(cancel_token);
        return;
      }

      if (observe_cancellation && isCancellationRequested(cancel_state_ptr)) {
        emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::CANCELED);
        return;
      }

      emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::STARTED);
      fn(cancel_token);
      emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::FINISHED);
    };
  }
  else {
    return [desc_, runtime_state_, fn = std::forward<Fn>(fn_)]() mutable {
      const auto* cancel_state_ptr = runtime_state_->cancel_state.get();
      const auto trace_enabled = hasTaskTraceHook(*runtime_state_);
      const auto observe_cancellation = canObserveCancellation(desc_, cancel_state_ptr);

      if (!trace_enabled && !observe_cancellation) {
        fn();
        return;
      }

      if (observe_cancellation && isCancellationRequested(cancel_state_ptr)) {
        emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::CANCELED);
        return;
      }

      emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::STARTED);
      fn();
      emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::FINISHED);
    };
  }
}

template <class Index, class Fn>
[[nodiscard]] auto makeIndexedTaskInvoker(const TaskDesc& desc_,
                                          const std::shared_ptr<PlanRuntimeState>& runtime_state_,
                                          Fn&& fn_)
{
  if constexpr (std::invocable<Fn&, Index, const CancelToken&>) {
    return [desc_,
            runtime_state_,
            fn = std::forward<Fn>(fn_)](Index index_) mutable {
      auto cancel_token = CancelToken{runtime_state_->cancel_state};
      const auto* cancel_state_ptr = runtime_state_->cancel_state.get();
      const auto trace_enabled = hasTaskTraceHook(*runtime_state_);
      const auto observe_cancellation = canObserveCancellation(desc_, cancel_state_ptr);

      if (!trace_enabled && !observe_cancellation) {
        fn(index_, cancel_token);
        return;
      }

      if (observe_cancellation && isCancellationRequested(cancel_state_ptr)) {
        emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::CANCELED);
        return;
      }

      emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::STARTED);
      fn(index_, cancel_token);
      emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::FINISHED);
    };
  }
  else {
    return [desc_, runtime_state_, fn = std::forward<Fn>(fn_)](Index index_) mutable {
      const auto* cancel_state_ptr = runtime_state_->cancel_state.get();
      const auto trace_enabled = hasTaskTraceHook(*runtime_state_);
      const auto observe_cancellation = canObserveCancellation(desc_, cancel_state_ptr);

      if (!trace_enabled && !observe_cancellation) {
        fn(index_);
        return;
      }

      if (observe_cancellation && isCancellationRequested(cancel_state_ptr)) {
        emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::CANCELED);
        return;
      }

      emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::STARTED);
      fn(index_);
      emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::FINISHED);
    };
  }
}

template <class Fn>
[[nodiscard]] auto makeConditionInvoker(const TaskDesc& desc_,
                                        const std::shared_ptr<PlanRuntimeState>& runtime_state_,
                                        Fn&& fn_)
{
  if constexpr (std::invocable<Fn&, const CancelToken&>) {
    return [desc_,
            runtime_state_,
            fn = std::forward<Fn>(fn_)]() mutable {
      auto cancel_token = CancelToken{runtime_state_->cancel_state};
      const auto* cancel_state_ptr = runtime_state_->cancel_state.get();
      const auto trace_enabled = hasTaskTraceHook(*runtime_state_);
      const auto observe_cancellation = canObserveCancellation(desc_, cancel_state_ptr);

      if (!trace_enabled && !observe_cancellation) {
        return static_cast<int>(fn(cancel_token));
      }

      if (observe_cancellation && isCancellationRequested(cancel_state_ptr)) {
        emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::CANCELED);
        return 0;
      }

      emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::STARTED);
      auto result = static_cast<int>(fn(cancel_token));
      emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::FINISHED);
      return result;
    };
  }
  else {
    return [desc_, runtime_state_, fn = std::forward<Fn>(fn_)]() mutable {
      const auto* cancel_state_ptr = runtime_state_->cancel_state.get();
      const auto trace_enabled = hasTaskTraceHook(*runtime_state_);
      const auto observe_cancellation = canObserveCancellation(desc_, cancel_state_ptr);

      if (!trace_enabled && !observe_cancellation) {
        return static_cast<int>(fn());
      }

      if (observe_cancellation && isCancellationRequested(cancel_state_ptr)) {
        emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::CANCELED);
        return 0;
      }

      emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::STARTED);
      auto result = static_cast<int>(fn());
      emitTaskEvent(runtime_state_->trace_hooks, desc_, TaskEventType::FINISHED);
      return result;
    };
  }
}

[[nodiscard]] inline auto normalizeGateDesc(TaskDesc desc_) -> TaskDesc
{
  desc_.task_kind = TaskKind::GATE;
  return desc_;
}

[[nodiscard]] inline auto normalizeMainThreadGateDesc(TaskDesc desc_) -> TaskDesc
{
  desc_ = normalizeGateDesc(desc_);
  desc_.flags |= TaskFlags::MAIN_THREAD_GATE;

  if (desc_.lane == ExecutionLane::FRAME_NORMAL) {
    desc_.lane = ExecutionLane::FRAME_CRITICAL;
  }

  if (desc_.priority == TaskPriority::NORMAL) {
    desc_.priority = TaskPriority::CRITICAL;
  }

  if (desc_.budget_class == BudgetClass::FRAME_BOUND) {
    desc_.budget_class = BudgetClass::REALTIME;
  }

  return desc_;
}

[[nodiscard]] constexpr auto scheduleDomainForPhase(FramePhase phase_) noexcept -> ScheduleDomain
{
  switch (phase_) {
  case FramePhase::INPUT:
  case FramePhase::END_FRAME:
    return ScheduleDomain::FRAME;
  case FramePhase::EARLY_UPDATE:
  case FramePhase::GAMEPLAY:
  case FramePhase::AI:
    return ScheduleDomain::GAMEPLAY;
  case FramePhase::PHYSICS:
    return ScheduleDomain::PHYSICS;
  case FramePhase::ANIMATION:
    return ScheduleDomain::ANIMATION;
  case FramePhase::CULLING:
  case FramePhase::RENDER_PREP:
  case FramePhase::RENDER_SUBMIT:
    return ScheduleDomain::RENDERING;
  case FramePhase::STREAMING_FINALIZE:
    return ScheduleDomain::STREAMING;
  }

  return ScheduleDomain::FRAME;
}

[[nodiscard]] inline auto normalizeFrameTaskDesc(FramePhase phase_, TaskDesc desc_) -> TaskDesc
{
  desc_.domain = scheduleDomainForPhase(phase_);
  desc_.frame_phase_tag = static_cast<std::uint8_t>(phase_);
  return desc_;
}

[[nodiscard]] inline auto normalizeFrameSystemTaskDesc(const FrameSystemDesc& system_desc_)
  -> TaskDesc
{
  auto desc = normalizeFrameTaskDesc(system_desc_.phase, system_desc_.task_desc);

  if (desc.name.empty()) {
    desc.name = system_desc_.name;
  }

  return desc;
}

[[nodiscard]] inline auto normalizePipelineStageTaskDesc(const PipelineStageDesc& stage_desc_)
  -> TaskDesc
{
  auto desc = stage_desc_.task_desc;
  desc.task_kind = TaskKind::PIPELINE;

  if (desc.name.empty()) {
    desc.name = stage_desc_.name;
  }

  return desc;
}

[[nodiscard]] inline auto
makeTracedPipelineStageSpecs(const TaskDesc& pipeline_desc_,
                             const std::shared_ptr<PlanRuntimeState>& runtime_state_,
                             std::vector<PipelineBuilder::StageSpec> stage_specs_)
  -> std::vector<PipelineBuilder::StageSpec>
{
  for (std::size_t stage_index = 0; stage_index < stage_specs_.size(); ++stage_index) {
    auto stage_desc = stage_specs_[stage_index].desc;
    auto original_callback = std::move(stage_specs_[stage_index].callback);
    stage_specs_[stage_index].callback =
      [pipeline_name = pipeline_desc_.name,
       runtime_state = runtime_state_,
       stage_desc,
       stage_index,
       callback = std::move(original_callback)](PipelineTokenContext& token_context_,
                                                const CancelToken& cancel_token_) mutable {
        emitPipelineEvent(runtime_state->trace_hooks,
                          PipelineEventType::STAGE_BEGIN,
                          pipeline_name,
                          stage_desc,
                          stage_index,
                          token_context_.tokenIndex());
        const auto stop_requested_before = token_context_.stopRequested();
        callback(token_context_, cancel_token_);
        const auto stop_requested_after = token_context_.stopRequested();
        if (!stop_requested_before && stop_requested_after) {
          emitPipelineEvent(runtime_state->trace_hooks,
                            PipelineEventType::STOP_REQUESTED,
                            pipeline_name,
                            stage_desc,
                            stage_index,
                            token_context_.tokenIndex());
        }
        emitPipelineEvent(runtime_state->trace_hooks,
                          PipelineEventType::STAGE_END,
                          pipeline_name,
                          stage_desc,
                          stage_index,
                          token_context_.tokenIndex());
      };
  }

  return stage_specs_;
}

class FrameRuntimeCollector final
{
public:
  FrameRuntimeCollector() = default;

  void prepareDispatch(PlanRuntimeState& runtime_state_, const TraceHooks& user_trace_hooks_)
  {
    this->user_trace_hooks_ = user_trace_hooks_;
    resetDispatchState();
    runtime_state_.trace_hooks = TraceHooks{
      .user_data = this,
      .on_task_event = &onTaskEvent,
      .on_dispatch_event = &onDispatchEvent,
      .on_pipeline_event = &onPipelineEvent,
    };
  }

  [[nodiscard]] auto snapshot() const -> FrameRuntimeReport
  {
    FrameRuntimeReport report;
    report.dispatch_count = dispatch_count_.load(std::memory_order_relaxed);

    for (std::size_t phase_index = 0; phase_index < kFramePhaseCount; ++phase_index) {
      const auto started_count =
        phase_stats_[phase_index].started_count.load(std::memory_order_relaxed);
      const auto finished_count =
        phase_stats_[phase_index].finished_count.load(std::memory_order_relaxed);
      const auto canceled_count =
        phase_stats_[phase_index].canceled_count.load(std::memory_order_relaxed);
      const auto begin_event_count =
        phase_stats_[phase_index].begin_event_count.load(std::memory_order_relaxed);
      const auto end_event_count =
        phase_stats_[phase_index].end_event_count.load(std::memory_order_relaxed);
      if (started_count == 0 && finished_count == 0 && canceled_count == 0 &&
          begin_event_count == 0 && end_event_count == 0) {
        continue;
      }

      report.phase_profiles.push_back(PhaseRuntimeProfile{
        .phase = static_cast<FramePhase>(phase_index),
        .started_count = started_count,
        .finished_count = finished_count,
        .canceled_count = canceled_count,
        .begin_event_count = begin_event_count,
        .end_event_count = end_event_count,
        .last_begin_offset_ns =
          phase_stats_[phase_index].last_begin_offset_ns.load(std::memory_order_relaxed),
        .last_end_offset_ns =
          phase_stats_[phase_index].last_end_offset_ns.load(std::memory_order_relaxed),
      });
    }

    for (std::size_t lane_index = 0; lane_index < kExecutionLaneCount; ++lane_index) {
      const auto started_count =
        lane_stats_[lane_index].started_count.load(std::memory_order_relaxed);
      const auto finished_count =
        lane_stats_[lane_index].finished_count.load(std::memory_order_relaxed);
      const auto canceled_count =
        lane_stats_[lane_index].canceled_count.load(std::memory_order_relaxed);
      if (started_count == 0 && finished_count == 0 && canceled_count == 0) {
        continue;
      }

      report.lane_profiles.push_back(ExecutionLaneRuntimeProfile{
        .lane = static_cast<ExecutionLane>(lane_index),
        .started_count = started_count,
        .finished_count = finished_count,
        .canceled_count = canceled_count,
      });
    }

    const auto total_dispatch_wall_time_ns =
      accumulated_dispatch_wall_time_ns_.load(std::memory_order_relaxed);

    for (std::size_t worker_id = 0; worker_id < worker_count_; ++worker_id) {
      const auto busy_time_ns =
        worker_states_[worker_id].busy_time_ns.load(std::memory_order_relaxed);
      const auto started_count =
        worker_states_[worker_id].started_count.load(std::memory_order_relaxed);
      const auto finished_count =
        worker_states_[worker_id].finished_count.load(std::memory_order_relaxed);
      if (started_count == 0 && finished_count == 0) {
        continue;
      }

      const auto utilization_permille =
        total_dispatch_wall_time_ns == 0
          ? 0u
          : static_cast<std::uint32_t>((busy_time_ns * 1000u) / total_dispatch_wall_time_ns);
      report.worker_profiles.push_back(WorkerRuntimeProfile{
        .worker_id = static_cast<std::uint32_t>(worker_id),
        .started_count = started_count,
        .finished_count = finished_count,
        .max_queue_size_observed =
          worker_states_[worker_id].max_queue_size_observed.load(std::memory_order_relaxed),
        .busy_time_ns = busy_time_ns,
        .last_entry_offset_ns =
          worker_states_[worker_id].last_entry_offset_ns.load(std::memory_order_relaxed),
        .last_exit_offset_ns =
          worker_states_[worker_id].last_exit_offset_ns.load(std::memory_order_relaxed),
        .utilization_permille = utilization_permille,
      });
    }

    report.pipeline_profile = PipelineRuntimeProfile{
      .stage_begin_count = pipeline_stage_begin_count_.load(std::memory_order_relaxed),
      .stage_end_count = pipeline_stage_end_count_.load(std::memory_order_relaxed),
      .stop_requested_count = pipeline_stop_requested_count_.load(std::memory_order_relaxed),
    };

    return report;
  }

public:
  void setWorkerCount(std::size_t requested_worker_count_)
  {
    if (worker_count_ == requested_worker_count_) {
      return;
    }

    worker_states_ = std::make_unique<WorkerRuntimeState[]>(requested_worker_count_);
    worker_count_ = requested_worker_count_;
  }

  void handleWorkerEntry(std::uint32_t worker_id_, std::uint32_t queue_size_)
  {
    if (worker_id_ >= worker_count_) {
      return;
    }

    auto& worker_state = worker_states_[worker_id_];
    worker_state.started_count.fetch_add(1, std::memory_order_relaxed);
    worker_state.active_begin_ns.store(nowNs(), std::memory_order_relaxed);
    worker_state.last_entry_offset_ns.store(dispatchOffsetNs(), std::memory_order_relaxed);

    auto observed_queue_size = worker_state.max_queue_size_observed.load(std::memory_order_relaxed);
    while (
      observed_queue_size < queue_size_ &&
      !worker_state.max_queue_size_observed.compare_exchange_weak(
        observed_queue_size, queue_size_, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
  }

  void handleWorkerExit(std::uint32_t worker_id_, std::uint32_t queue_size_)
  {
    if (worker_id_ >= worker_count_) {
      return;
    }

    auto& worker_state = worker_states_[worker_id_];
    worker_state.finished_count.fetch_add(1, std::memory_order_relaxed);
    worker_state.last_exit_offset_ns.store(dispatchOffsetNs(), std::memory_order_relaxed);

    const auto begin_ns = worker_state.active_begin_ns.load(std::memory_order_relaxed);
    const auto end_ns = nowNs();
    if (end_ns > begin_ns) {
      worker_state.busy_time_ns.fetch_add(end_ns - begin_ns, std::memory_order_relaxed);
    }

    auto observed_queue_size = worker_state.max_queue_size_observed.load(std::memory_order_relaxed);
    while (
      observed_queue_size < queue_size_ &&
      !worker_state.max_queue_size_observed.compare_exchange_weak(
        observed_queue_size, queue_size_, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
  }

private:
  static constexpr std::size_t kFramePhaseCount =
    static_cast<std::size_t>(FramePhase::END_FRAME) + std::size_t{1};
  static constexpr std::size_t kExecutionLaneCount =
    static_cast<std::size_t>(ExecutionLane::SERVICE) + std::size_t{1};

  struct PhaseRuntimeState final
  {
    std::atomic_uint32_t started_count{0};
    std::atomic_uint32_t finished_count{0};
    std::atomic_uint32_t canceled_count{0};
    std::atomic_uint32_t begin_event_count{0};
    std::atomic_uint32_t end_event_count{0};
    std::atomic_uint32_t in_flight_count{0};
    std::atomic_uint64_t last_begin_offset_ns{0};
    std::atomic_uint64_t last_end_offset_ns{0};
  };

  struct LaneRuntimeState final
  {
    std::atomic_uint32_t started_count{0};
    std::atomic_uint32_t finished_count{0};
    std::atomic_uint32_t canceled_count{0};
  };

  struct WorkerRuntimeState final
  {
    std::atomic_uint32_t started_count{0};
    std::atomic_uint32_t finished_count{0};
    std::atomic_uint32_t max_queue_size_observed{0};
    std::atomic_uint64_t busy_time_ns{0};
    std::atomic_uint64_t active_begin_ns{0};
    std::atomic_uint64_t last_entry_offset_ns{0};
    std::atomic_uint64_t last_exit_offset_ns{0};
  };

  static void onTaskEvent(void* user_data_, const TaskEvent& event_)
  {
    auto* collector = static_cast<FrameRuntimeCollector*>(user_data_);
    collector->handleTaskEvent(event_);
    if (collector->user_trace_hooks_.on_task_event != nullptr) {
      collector->user_trace_hooks_.on_task_event(collector->user_trace_hooks_.user_data, event_);
    }
  }

  static void onDispatchEvent(void* user_data_, const DispatchEvent& event_)
  {
    auto* collector = static_cast<FrameRuntimeCollector*>(user_data_);
    collector->handleDispatchEvent(event_);
    if (collector->user_trace_hooks_.on_dispatch_event != nullptr) {
      collector->user_trace_hooks_.on_dispatch_event(collector->user_trace_hooks_.user_data,
                                                     event_);
    }
  }

  static void onPipelineEvent(void* user_data_, const PipelineEvent& event_)
  {
    auto* collector = static_cast<FrameRuntimeCollector*>(user_data_);
    collector->handlePipelineEvent(event_);
    if (collector->user_trace_hooks_.on_pipeline_event != nullptr) {
      collector->user_trace_hooks_.on_pipeline_event(collector->user_trace_hooks_.user_data,
                                                     event_);
    }
  }

  void resetDispatchState()
  {
    dispatch_start_ns_.store(nowNs(), std::memory_order_relaxed);
    for (auto& phase_state : phase_stats_) {
      phase_state.in_flight_count.store(0, std::memory_order_relaxed);
      phase_state.last_begin_offset_ns.store(0, std::memory_order_relaxed);
      phase_state.last_end_offset_ns.store(0, std::memory_order_relaxed);
    }
  }

  [[nodiscard]] static auto nowNs() noexcept -> std::uint64_t
  {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
  }

  [[nodiscard]] auto dispatchOffsetNs() const noexcept -> std::uint64_t
  {
    const auto start_ns = dispatch_start_ns_.load(std::memory_order_relaxed);
    const auto now_ns = nowNs();
    return now_ns >= start_ns ? now_ns - start_ns : 0;
  }

  void handleDispatchEvent(const DispatchEvent& event_)
  {
    if (event_.type == DispatchEventType::BEGIN) {
      dispatch_count_.fetch_add(1, std::memory_order_relaxed);
      dispatch_start_ns_.store(nowNs(), std::memory_order_relaxed);
    }
    else if (event_.type == DispatchEventType::END) {
      accumulated_dispatch_wall_time_ns_.fetch_add(dispatchOffsetNs(), std::memory_order_relaxed);
    }
  }

  void handleTaskEvent(const TaskEvent& event_)
  {
    auto& lane_state = lane_stats_[static_cast<std::size_t>(event_.lane)];
    switch (event_.type) {
    case TaskEventType::STARTED:
      lane_state.started_count.fetch_add(1, std::memory_order_relaxed);
      break;
    case TaskEventType::FINISHED:
      lane_state.finished_count.fetch_add(1, std::memory_order_relaxed);
      break;
    case TaskEventType::CANCELED:
      lane_state.canceled_count.fetch_add(1, std::memory_order_relaxed);
      break;
    case TaskEventType::SUBMITTED:
      break;
    }

    if (!hasFramePhaseTag(event_.frame_phase_tag)) {
      return;
    }

    auto& phase_state = phase_stats_[static_cast<std::size_t>(event_.frame_phase_tag)];
    switch (event_.type) {
    case TaskEventType::STARTED: {
      phase_state.started_count.fetch_add(1, std::memory_order_relaxed);
      const auto previous_in_flight =
        phase_state.in_flight_count.fetch_add(1, std::memory_order_acq_rel);
      if (previous_in_flight == 0) {
        phase_state.begin_event_count.fetch_add(1, std::memory_order_relaxed);
        phase_state.last_begin_offset_ns.store(dispatchOffsetNs(), std::memory_order_relaxed);
      }
      break;
    }
    case TaskEventType::FINISHED: {
      phase_state.finished_count.fetch_add(1, std::memory_order_relaxed);
      const auto previous_in_flight =
        phase_state.in_flight_count.fetch_sub(1, std::memory_order_acq_rel);
      if (previous_in_flight == 1) {
        phase_state.end_event_count.fetch_add(1, std::memory_order_relaxed);
        phase_state.last_end_offset_ns.store(dispatchOffsetNs(), std::memory_order_relaxed);
      }
      break;
    }
    case TaskEventType::CANCELED:
      phase_state.canceled_count.fetch_add(1, std::memory_order_relaxed);
      break;
    case TaskEventType::SUBMITTED:
      break;
    }
  }

  void handlePipelineEvent(const PipelineEvent& event_)
  {
    switch (event_.type) {
    case PipelineEventType::STAGE_BEGIN:
      pipeline_stage_begin_count_.fetch_add(1, std::memory_order_relaxed);
      break;
    case PipelineEventType::STAGE_END:
      pipeline_stage_end_count_.fetch_add(1, std::memory_order_relaxed);
      break;
    case PipelineEventType::STOP_REQUESTED:
      pipeline_stop_requested_count_.fetch_add(1, std::memory_order_relaxed);
      break;
    }
  }

  TraceHooks user_trace_hooks_{};
  std::atomic_uint32_t dispatch_count_{0};
  std::atomic_uint64_t dispatch_start_ns_{0};
  std::atomic_uint64_t accumulated_dispatch_wall_time_ns_{0};
  std::array<PhaseRuntimeState, kFramePhaseCount> phase_stats_{};
  std::array<LaneRuntimeState, kExecutionLaneCount> lane_stats_{};
  std::unique_ptr<WorkerRuntimeState[]> worker_states_{};
  std::size_t worker_count_{0};
  std::atomic_uint32_t pipeline_stage_begin_count_{0};
  std::atomic_uint32_t pipeline_stage_end_count_{0};
  std::atomic_uint32_t pipeline_stop_requested_count_{0};
};

class TaskflowRuntimeObserver final : public tf::ObserverInterface
{
public:
  explicit TaskflowRuntimeObserver(
    std::atomic<FrameRuntimeCollector*>* active_runtime_collector_) noexcept
      : active_runtime_collector_(active_runtime_collector_)
  {
  }

  void set_up(std::size_t) override {}

  void on_entry(tf::WorkerView worker_view_, tf::TaskView) override
  {
    if (active_runtime_collector_ == nullptr) {
      return;
    }

    auto* collector = active_runtime_collector_->load(std::memory_order_relaxed);
    if (collector != nullptr) {
      collector->handleWorkerEntry(static_cast<std::uint32_t>(worker_view_.id()),
                                   static_cast<std::uint32_t>(worker_view_.queue_size()));
    }
  }

  void on_exit(tf::WorkerView worker_view_, tf::TaskView) override
  {
    if (active_runtime_collector_ == nullptr) {
      return;
    }

    auto* collector = active_runtime_collector_->load(std::memory_order_relaxed);
    if (collector != nullptr) {
      collector->handleWorkerExit(static_cast<std::uint32_t>(worker_view_.id()),
                                  static_cast<std::uint32_t>(worker_view_.queue_size()));
    }
  }

private:
  std::atomic<FrameRuntimeCollector*>* active_runtime_collector_{nullptr};
};

template <class Backend>
concept BackendAdapter = requires(typename Backend::EngineType& engine_,
                                  typename Backend::GraphType& graph_,
                                  typename Backend::SubflowType& subflow_,
                                  typename Backend::NodeType from_,
                                  typename Backend::NodeType to_,
                                  typename Backend::RunHandleType& run_handle_,
                                  const ExecutorConfig& executor_config_,
                                  const TaskDesc& desc_) {
  typename Backend::EngineType;
  typename Backend::GraphType;
  typename Backend::SubflowType;
  typename Backend::NodeType;
  typename Backend::RunHandleType;
  { Backend::makeEngine(executor_config_) } -> std::same_as<typename Backend::EngineType>;
  { Backend::makeGraph() } -> std::same_as<typename Backend::GraphType>;
  { Backend::depend(from_, to_) } -> std::same_as<void>;
  { Backend::dispatch(engine_, graph_) } -> std::same_as<typename Backend::RunHandleType>;
  { Backend::wait(run_handle_) } -> std::same_as<void>;
  { Backend::waitIdle(engine_) } -> std::same_as<void>;
  { Backend::clear(graph_) } -> std::same_as<void>;
  { Backend::decorate(from_, desc_) } -> std::same_as<void>;
  { Backend::join(subflow_) } -> std::same_as<void>;
};

} // namespace Detail

inline CancelSource::CancelSource() : state_(std::make_shared<Detail::CancelState>()) {}

inline auto CancelSource::token() const -> CancelToken
{
  return CancelToken{state_};
}

inline void CancelSource::requestStop() const noexcept
{
  if (state_) {
    state_->stop_requested.store(true, std::memory_order_relaxed);
  }
}

inline void CancelSource::reset()
{
  state_ = std::make_shared<Detail::CancelState>();
}

inline auto CancelToken::stopRequested() const noexcept -> bool
{
  return state_ != nullptr && state_->stop_requested.load(std::memory_order_relaxed);
}

} // namespace ThreadCenter
}

namespace ThreadCenter::Detail
{

inline auto TaskflowBackend::makeEngine(const ExecutorConfig& config_) -> EngineType
{
  return EngineType{config_.workers};
}

inline auto TaskflowBackend::makeGraph() -> GraphType
{
  return GraphType{};
}

inline void TaskflowBackend::depend(NodeType from_, NodeType to_)
{
  from_.precede(to_);
}

inline auto TaskflowBackend::dispatch(EngineType& engine_, GraphType& graph_) -> RunHandleType
{
  return engine_.run(graph_);
}

inline void TaskflowBackend::wait(RunHandleType& run_handle_)
{
  run_handle_.wait();
}

inline void TaskflowBackend::waitIdle(EngineType& engine_)
{
  engine_.wait_for_all();
}

inline void TaskflowBackend::clear(GraphType& graph_)
{
  graph_.clear();
}

inline void TaskflowBackend::decorate(NodeType task_, const TaskDesc& desc_)
{
  if (!desc_.name.empty() && hasFlag(desc_.flags, TaskFlags::BACKEND_NAME)) {
    task_.name(std::string(desc_.name));
  }
}

} // namespace ThreadCenter::Detail
