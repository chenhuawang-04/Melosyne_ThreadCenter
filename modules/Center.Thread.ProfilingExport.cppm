module;

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

export module Center.Thread.ProfilingExport;

import Center.Thread.Common;
import Center.Thread.FramePlan;

export {
namespace ThreadCenter::Profiling
{

struct JsonExportOptions final
{
  bool pretty{true};
  std::string indent{"  "};
};

namespace Detail
{

inline void
appendIndent(std::string& output_, const JsonExportOptions& options_, std::size_t level_)
{
  if (!options_.pretty) {
    return;
  }

  for (std::size_t index = 0; index < level_; ++index) {
    output_ += options_.indent;
  }
}

inline void appendNewline(std::string& output_, const JsonExportOptions& options_)
{
  if (options_.pretty) {
    output_ += '\n';
  }
}

inline void appendEscapedString(std::string& output_, std::string_view value_)
{
  output_ += '"';
  for (const auto ch_ : value_) {
    switch (ch_) {
    case '\\':
      output_ += "\\\\";
      break;
    case '"':
      output_ += "\\\"";
      break;
    case '\n':
      output_ += "\\n";
      break;
    case '\r':
      output_ += "\\r";
      break;
    case '\t':
      output_ += "\\t";
      break;
    default:
      output_ += ch_;
      break;
    }
  }
  output_ += '"';
}

inline void beginObject(std::string& output_, const JsonExportOptions& options_)
{
  output_ += '{';
  appendNewline(output_, options_);
}

inline void endObject(std::string& output_, const JsonExportOptions& options_, std::size_t level_)
{
  appendIndent(output_, options_, level_);
  output_ += '}';
}

inline void beginArray(std::string& output_, const JsonExportOptions& options_)
{
  output_ += '[';
  appendNewline(output_, options_);
}

inline void endArray(std::string& output_, const JsonExportOptions& options_, std::size_t level_)
{
  appendIndent(output_, options_, level_);
  output_ += ']';
}

inline void appendFieldSeparator(std::string& output_, const JsonExportOptions& options_)
{
  output_ += ',';
  appendNewline(output_, options_);
}

inline void appendKey(std::string& output_,
                      std::string_view key_,
                      const JsonExportOptions& options_,
                      std::size_t level_)
{
  appendIndent(output_, options_, level_);
  appendEscapedString(output_, key_);
  output_ += options_.pretty ? ": " : ":";
}

inline auto framePhaseName(FramePhase phase_) -> std::string_view
{
  switch (phase_) {
  case FramePhase::INPUT:
    return "INPUT";
  case FramePhase::EARLY_UPDATE:
    return "EARLY_UPDATE";
  case FramePhase::GAMEPLAY:
    return "GAMEPLAY";
  case FramePhase::PHYSICS:
    return "PHYSICS";
  case FramePhase::ANIMATION:
    return "ANIMATION";
  case FramePhase::AI:
    return "AI";
  case FramePhase::CULLING:
    return "CULLING";
  case FramePhase::RENDER_PREP:
    return "RENDER_PREP";
  case FramePhase::RENDER_SUBMIT:
    return "RENDER_SUBMIT";
  case FramePhase::STREAMING_FINALIZE:
    return "STREAMING_FINALIZE";
  case FramePhase::END_FRAME:
    return "END_FRAME";
  }

  return "UNKNOWN_PHASE";
}

inline auto executionLaneName(ExecutionLane lane_) -> std::string_view
{
  switch (lane_) {
  case ExecutionLane::FRAME_CRITICAL:
    return "FRAME_CRITICAL";
  case ExecutionLane::FRAME_NORMAL:
    return "FRAME_NORMAL";
  case ExecutionLane::BACKGROUND:
    return "BACKGROUND";
  case ExecutionLane::STREAMING:
    return "STREAMING";
  case ExecutionLane::BLOCKING:
    return "BLOCKING";
  case ExecutionLane::RENDER_ASSIST:
    return "RENDER_ASSIST";
  case ExecutionLane::SERVICE:
    return "SERVICE";
  }

  return "UNKNOWN_LANE";
}

inline auto taskKindName(TaskKind task_kind_) -> std::string_view
{
  switch (task_kind_) {
  case TaskKind::COMPUTE:
    return "COMPUTE";
  case TaskKind::PARALLEL_FOR:
    return "PARALLEL_FOR";
  case TaskKind::PARALLEL_REDUCE:
    return "PARALLEL_REDUCE";
  case TaskKind::DYNAMIC:
    return "DYNAMIC";
  case TaskKind::CONDITION:
    return "CONDITION";
  case TaskKind::PIPELINE:
    return "PIPELINE";
  case TaskKind::EXTERNAL:
    return "EXTERNAL";
  case TaskKind::GATE:
    return "GATE";
  case TaskKind::DAEMON:
    return "DAEMON";
  }

  return "UNKNOWN_TASK_KIND";
}

inline auto pipelineStageKindName(PipelineStageKind stage_kind_) -> std::string_view
{
  switch (stage_kind_) {
  case PipelineStageKind::SERIAL:
    return "SERIAL";
  case PipelineStageKind::PARALLEL:
    return "PARALLEL";
  }

  return "UNKNOWN_STAGE_KIND";
}

template <class Fn>
void appendArray(std::string& output_,
                 const JsonExportOptions& options_,
                 std::size_t level_,
                 std::size_t count_,
                 Fn&& append_element_)
{
  beginArray(output_, options_);
  for (std::size_t index = 0; index < count_; ++index) {
    append_element_(index);
    if (index + 1 != count_) {
      appendFieldSeparator(output_, options_);
    }
    else {
      appendNewline(output_, options_);
    }
  }
  endArray(output_, options_, level_);
}

inline void appendPhaseProfile(std::string& output_,
                               const FramePhaseProfile& phase_profile_,
                               const JsonExportOptions& options_,
                               std::size_t level_)
{
  beginObject(output_, options_);
  appendKey(output_, "phase", options_, level_ + 1);
  appendEscapedString(output_, framePhaseName(phase_profile_.phase));
  appendFieldSeparator(output_, options_);
  appendKey(output_, "task_count", options_, level_ + 1);
  output_ += std::to_string(phase_profile_.task_count);
  appendFieldSeparator(output_, options_);
  appendKey(output_, "gate_count", options_, level_ + 1);
  output_ += std::to_string(phase_profile_.gate_count);
  appendFieldSeparator(output_, options_);
  appendKey(output_, "estimated_cost_ns", options_, level_ + 1);
  output_ += std::to_string(phase_profile_.estimated_cost_ns);
  appendNewline(output_, options_);
  endObject(output_, options_, level_);
}

inline void appendLaneProfile(std::string& output_,
                              const ExecutionLaneProfile& lane_profile_,
                              const JsonExportOptions& options_,
                              std::size_t level_)
{
  beginObject(output_, options_);
  appendKey(output_, "lane", options_, level_ + 1);
  appendEscapedString(output_, executionLaneName(lane_profile_.lane));
  appendFieldSeparator(output_, options_);
  appendKey(output_, "task_count", options_, level_ + 1);
  output_ += std::to_string(lane_profile_.task_count);
  appendFieldSeparator(output_, options_);
  appendKey(output_, "estimated_cost_ns", options_, level_ + 1);
  output_ += std::to_string(lane_profile_.estimated_cost_ns);
  appendNewline(output_, options_);
  endObject(output_, options_, level_);
}

inline void appendCriticalPathNode(std::string& output_,
                                   const CriticalPathNode& critical_path_node_,
                                   const JsonExportOptions& options_,
                                   std::size_t level_)
{
  beginObject(output_, options_);
  appendKey(output_, "node_id", options_, level_ + 1);
  output_ += std::to_string(critical_path_node_.node_id);
  appendFieldSeparator(output_, options_);
  appendKey(output_, "name", options_, level_ + 1);
  appendEscapedString(output_, critical_path_node_.name);
  appendFieldSeparator(output_, options_);
  appendKey(output_, "phase", options_, level_ + 1);
  appendEscapedString(output_, framePhaseName(critical_path_node_.phase));
  appendFieldSeparator(output_, options_);
  appendKey(output_, "lane", options_, level_ + 1);
  appendEscapedString(output_, executionLaneName(critical_path_node_.lane));
  appendFieldSeparator(output_, options_);
  appendKey(output_, "task_kind", options_, level_ + 1);
  appendEscapedString(output_, taskKindName(critical_path_node_.task_kind));
  appendFieldSeparator(output_, options_);
  appendKey(output_, "estimated_cost_ns", options_, level_ + 1);
  output_ += std::to_string(critical_path_node_.estimated_cost_ns);
  appendNewline(output_, options_);
  endObject(output_, options_, level_);
}

inline void appendPhaseRuntimeProfile(std::string& output_,
                                      const PhaseRuntimeProfile& phase_runtime_,
                                      const JsonExportOptions& options_,
                                      std::size_t level_)
{
  beginObject(output_, options_);
  appendKey(output_, "phase", options_, level_ + 1);
  appendEscapedString(output_, framePhaseName(phase_runtime_.phase));
  appendFieldSeparator(output_, options_);
  appendKey(output_, "started_count", options_, level_ + 1);
  output_ += std::to_string(phase_runtime_.started_count);
  appendFieldSeparator(output_, options_);
  appendKey(output_, "finished_count", options_, level_ + 1);
  output_ += std::to_string(phase_runtime_.finished_count);
  appendFieldSeparator(output_, options_);
  appendKey(output_, "canceled_count", options_, level_ + 1);
  output_ += std::to_string(phase_runtime_.canceled_count);
  appendFieldSeparator(output_, options_);
  appendKey(output_, "begin_event_count", options_, level_ + 1);
  output_ += std::to_string(phase_runtime_.begin_event_count);
  appendFieldSeparator(output_, options_);
  appendKey(output_, "end_event_count", options_, level_ + 1);
  output_ += std::to_string(phase_runtime_.end_event_count);
  appendFieldSeparator(output_, options_);
  appendKey(output_, "last_begin_offset_ns", options_, level_ + 1);
  output_ += std::to_string(phase_runtime_.last_begin_offset_ns);
  appendFieldSeparator(output_, options_);
  appendKey(output_, "last_end_offset_ns", options_, level_ + 1);
  output_ += std::to_string(phase_runtime_.last_end_offset_ns);
  appendNewline(output_, options_);
  endObject(output_, options_, level_);
}

inline void appendLaneRuntimeProfile(std::string& output_,
                                     const ExecutionLaneRuntimeProfile& lane_runtime_,
                                     const JsonExportOptions& options_,
                                     std::size_t level_)
{
  beginObject(output_, options_);
  appendKey(output_, "lane", options_, level_ + 1);
  appendEscapedString(output_, executionLaneName(lane_runtime_.lane));
  appendFieldSeparator(output_, options_);
  appendKey(output_, "started_count", options_, level_ + 1);
  output_ += std::to_string(lane_runtime_.started_count);
  appendFieldSeparator(output_, options_);
  appendKey(output_, "finished_count", options_, level_ + 1);
  output_ += std::to_string(lane_runtime_.finished_count);
  appendFieldSeparator(output_, options_);
  appendKey(output_, "canceled_count", options_, level_ + 1);
  output_ += std::to_string(lane_runtime_.canceled_count);
  appendNewline(output_, options_);
  endObject(output_, options_, level_);
}

inline void appendWorkerRuntimeProfile(std::string& output_,
                                       const WorkerRuntimeProfile& worker_runtime_,
                                       const JsonExportOptions& options_,
                                       std::size_t level_)
{
  beginObject(output_, options_);
  appendKey(output_, "worker_id", options_, level_ + 1);
  output_ += std::to_string(worker_runtime_.worker_id);
  appendFieldSeparator(output_, options_);
  appendKey(output_, "started_count", options_, level_ + 1);
  output_ += std::to_string(worker_runtime_.started_count);
  appendFieldSeparator(output_, options_);
  appendKey(output_, "finished_count", options_, level_ + 1);
  output_ += std::to_string(worker_runtime_.finished_count);
  appendFieldSeparator(output_, options_);
  appendKey(output_, "max_queue_size_observed", options_, level_ + 1);
  output_ += std::to_string(worker_runtime_.max_queue_size_observed);
  appendFieldSeparator(output_, options_);
  appendKey(output_, "busy_time_ns", options_, level_ + 1);
  output_ += std::to_string(worker_runtime_.busy_time_ns);
  appendFieldSeparator(output_, options_);
  appendKey(output_, "last_entry_offset_ns", options_, level_ + 1);
  output_ += std::to_string(worker_runtime_.last_entry_offset_ns);
  appendFieldSeparator(output_, options_);
  appendKey(output_, "last_exit_offset_ns", options_, level_ + 1);
  output_ += std::to_string(worker_runtime_.last_exit_offset_ns);
  appendFieldSeparator(output_, options_);
  appendKey(output_, "utilization_permille", options_, level_ + 1);
  output_ += std::to_string(worker_runtime_.utilization_permille);
  appendNewline(output_, options_);
  endObject(output_, options_, level_);
}

inline void appendPipelineRuntimeProfile(std::string& output_,
                                         const PipelineRuntimeProfile& pipeline_runtime_,
                                         const JsonExportOptions& options_,
                                         std::size_t level_)
{
  beginObject(output_, options_);
  appendKey(output_, "stage_begin_count", options_, level_ + 1);
  output_ += std::to_string(pipeline_runtime_.stage_begin_count);
  appendFieldSeparator(output_, options_);
  appendKey(output_, "stage_end_count", options_, level_ + 1);
  output_ += std::to_string(pipeline_runtime_.stage_end_count);
  appendFieldSeparator(output_, options_);
  appendKey(output_, "stop_requested_count", options_, level_ + 1);
  output_ += std::to_string(pipeline_runtime_.stop_requested_count);
  appendNewline(output_, options_);
  endObject(output_, options_, level_);
}

} // namespace Detail

inline auto makeSnapshotJson(const FrameProfileReport& profile_report_,
                             const FrameRuntimeReport& runtime_report_,
                             const JsonExportOptions& options_ = {}) -> std::string
{
  auto output = std::string{};
  output.reserve(8192);

  Detail::beginObject(output, options_);

  Detail::appendKey(output, "static_profile", options_, 1);
  Detail::beginObject(output, options_);
  Detail::appendKey(output, "node_count", options_, 2);
  output += std::to_string(profile_report_.node_count);
  Detail::appendFieldSeparator(output, options_);
  Detail::appendKey(output, "edge_count", options_, 2);
  output += std::to_string(profile_report_.edge_count);
  Detail::appendFieldSeparator(output, options_);
  Detail::appendKey(output, "phase_profiles", options_, 2);
  Detail::appendArray(
    output, options_, 2, profile_report_.phase_profiles.size(), [&](std::size_t index_) {
      Detail::appendPhaseProfile(output, profile_report_.phase_profiles[index_], options_, 3);
    });
  Detail::appendFieldSeparator(output, options_);
  Detail::appendKey(output, "lane_profiles", options_, 2);
  Detail::appendArray(
    output, options_, 2, profile_report_.lane_profiles.size(), [&](std::size_t index_) {
      Detail::appendLaneProfile(output, profile_report_.lane_profiles[index_], options_, 3);
    });
  Detail::appendFieldSeparator(output, options_);
  Detail::appendKey(output, "critical_path", options_, 2);
  Detail::beginObject(output, options_);
  Detail::appendKey(output, "total_estimated_cost_ns", options_, 3);
  output += std::to_string(profile_report_.critical_path.total_estimated_cost_ns);
  Detail::appendFieldSeparator(output, options_);
  Detail::appendKey(output, "nodes", options_, 3);
  Detail::appendArray(
    output, options_, 3, profile_report_.critical_path.nodes.size(), [&](std::size_t index_) {
      Detail::appendCriticalPathNode(
        output, profile_report_.critical_path.nodes[index_], options_, 4);
    });
  Detail::appendNewline(output, options_);
  Detail::endObject(output, options_, 2);
  Detail::appendNewline(output, options_);
  Detail::endObject(output, options_, 1);

  Detail::appendFieldSeparator(output, options_);
  Detail::appendKey(output, "runtime_profile", options_, 1);
  Detail::beginObject(output, options_);
  Detail::appendKey(output, "dispatch_count", options_, 2);
  output += std::to_string(runtime_report_.dispatch_count);
  Detail::appendFieldSeparator(output, options_);
  Detail::appendKey(output, "phase_profiles", options_, 2);
  Detail::appendArray(
    output, options_, 2, runtime_report_.phase_profiles.size(), [&](std::size_t index_) {
      Detail::appendPhaseRuntimeProfile(
        output, runtime_report_.phase_profiles[index_], options_, 3);
    });
  Detail::appendFieldSeparator(output, options_);
  Detail::appendKey(output, "lane_profiles", options_, 2);
  Detail::appendArray(
    output, options_, 2, runtime_report_.lane_profiles.size(), [&](std::size_t index_) {
      Detail::appendLaneRuntimeProfile(output, runtime_report_.lane_profiles[index_], options_, 3);
    });
  Detail::appendFieldSeparator(output, options_);
  Detail::appendKey(output, "worker_profiles", options_, 2);
  Detail::appendArray(
    output, options_, 2, runtime_report_.worker_profiles.size(), [&](std::size_t index_) {
      Detail::appendWorkerRuntimeProfile(
        output, runtime_report_.worker_profiles[index_], options_, 3);
    });
  Detail::appendFieldSeparator(output, options_);
  Detail::appendKey(output, "pipeline_profile", options_, 2);
  Detail::appendPipelineRuntimeProfile(output, runtime_report_.pipeline_profile, options_, 2);
  Detail::appendNewline(output, options_);
  Detail::endObject(output, options_, 1);

  Detail::appendNewline(output, options_);
  Detail::endObject(output, options_, 0);
  Detail::appendNewline(output, options_);
  return output;
}

template <ThreadCenter::Detail::BackendAdapter Backend>
[[nodiscard]] inline auto makeSnapshotJson(const BasicCompiledFramePlan<Backend>& compiled_frame_,
                                           const JsonExportOptions& options_ = {}) -> std::string
{
  return makeSnapshotJson(
    compiled_frame_.profileReport(), compiled_frame_.runtimeReport(), options_);
}

inline void writeSnapshotJson(const std::filesystem::path& output_path_,
                              const FrameProfileReport& profile_report_,
                              const FrameRuntimeReport& runtime_report_,
                              const JsonExportOptions& options_ = {})
{
  auto output_stream = std::ofstream{output_path_, std::ios::binary};
  auto json = makeSnapshotJson(profile_report_, runtime_report_, options_);
  output_stream.write(json.data(), static_cast<std::streamsize>(json.size()));
}

template <ThreadCenter::Detail::BackendAdapter Backend>
inline void writeSnapshotJson(const std::filesystem::path& output_path_,
                              const BasicCompiledFramePlan<Backend>& compiled_frame_,
                              const JsonExportOptions& options_ = {})
{
  writeSnapshotJson(
    output_path_, compiled_frame_.profileReport(), compiled_frame_.runtimeReport(), options_);
}

} // namespace ThreadCenter::Profiling
}
