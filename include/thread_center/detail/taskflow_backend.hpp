#pragma once

#ifndef CENTER_THREAD_MODULE_BUILD
#include <future>
#include <thread>
#include <utility>

#include <taskflow/algorithm/for_each.hpp>
#include <taskflow/algorithm/pipeline.hpp>
#include <taskflow/taskflow.hpp>
#endif

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
