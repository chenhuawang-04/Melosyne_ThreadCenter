# ThreadCenter V2 设计方案

> 目标：把 ThreadCenter 从“最小可运行骨架”提升为一套面向游戏引擎的高性能统一并发控制中心。

本文档重点回答四个问题：

1. **ThreadCenter 最终应该负责什么**
2. **为了高性能，需要怎样的调度与执行模型**
3. **为了功能全面，需要开放哪些能力面**
4. **在当前 taskflow 后端下，哪些强能力应该保留并纳入正式抽象**

---

## 1. 设计立场

这版设计明确采用以下立场：

### 1.1 ThreadCenter 不是“线程池封装”

它应当是引擎的统一并发控制面，负责：

- **规划**：表达任务图、阶段、依赖、门控、批处理与资源边界
- **调度**：决定任务进入哪个执行域、以何种优先级、是否允许阻塞、是否允许窃取
- **执行**：把规划结果映射到真实后端并驱动 worker 运行
- **观测**：为 profiling、诊断、trace、预算统计提供统一数据出口
- **生命周期管理**：支持取消、场景切换、引擎关停、后台任务回收

### 1.2 不为了“多后端抽象”而削弱能力面

既然你已经明确：

> 未来自研线程库只会比 taskflow 功能更多，不会更少。

那么 ThreadCenter 的抽象就不应该被压缩到“最低公分母”。

设计原则应改为：

> **公共抽象要对齐引擎实际需要的高阶并发语义，并尽可能吸收 taskflow 的强能力。**

也就是说，ThreadCenter 允许公开这些高阶能力，只要它们是：

- 引擎真实需要的
- 将来自研库也会支持甚至增强的
- 不会把第三方具体类型泄漏到业务层

### 1.3 高性能优先于形式上的“完美抽象”

具体体现为：

- 热路径不使用虚函数
- 热路径不使用 `std::function`
- 构图尽量 arena / PMR 化
- 支持静态图复用
- 支持小任务批量化与图编译
- 支持长期任务隔离，避免 worker 被阻塞拖垮整个帧
- 观测点设计必须低侵入、低开销、可裁剪

---

## 2. 最终目标形态

ThreadCenter 最终应该是一个 **五层结构**。

## 2.1 Engine API 层

稳定对外接口，给引擎系统和业务系统使用。

建议长期保留的核心类型：

- `ThreadCenter::Center`
- `ThreadCenter::Plan`
- `ThreadCenter::FramePlan`
- `ThreadCenter::TaskHandle`
- `ThreadCenter::RunHandle`
- `ThreadCenter::TaskDesc`
- `ThreadCenter::CancelToken`
- `ThreadCenter::Barrier`
- `ThreadCenter::Gate`
- `ThreadCenter::TraceHooks`

这一层必须做到：

- 不暴露 taskflow 原生类型
- 接口稳定
- 具备表达引擎高级并发语义的能力

## 2.2 Planning 层

负责任务规划与图构建。

应该包含三类建模能力：

### A. 基础 DAG 建模

- 普通任务
- 批任务
- 依赖边
- join / fork
- 子图
- 条件分支
- pipeline

### B. 帧调度建模

- frame phase
- system group
- barrier
- main thread gate
- render submission gate
- streaming finalize gate

### C. 生命周期建模

- 可取消任务
- 场景绑定任务
- 热重载可中断任务
- 后台守护任务
- detached external completion task

## 2.3 Scheduling 层

负责把计划映射到调度策略。

核心能力应包括：

- priority class
- domain lane
- worker binding / affinity group
- blocking compensation
- long task isolation
- work stealing policy
- frame budget awareness
- main thread / render thread rendezvous

## 2.4 Backend Runtime 层

当前是 taskflow backend，未来可以替换为你的自研调度器。

这一层的职责是：

- 执行任务图
- 支持动态子图
- 支持条件任务
- 支持 pipeline
- 支持 async / external completion
- 支持 observer / trace 接口
- 支持 worker 维度观测与扩展

## 2.5 Instrumentation 层

负责可观测性与诊断。

必须覆盖：

- submit / ready / start / finish
- wait / block / wake
- steal / balance
- gate enter / leave
- frame budget consume
- long task alarm
- cancellation propagation

---

## 3. 能力面设计：ThreadCenter 应公开哪些功能

下面这些能力我建议纳入“正式设计”，而不是只做成后端细节。

## 3.1 基础任务能力

### 必备接口

- `task`：创建普通任务
- `parallelFor`：创建索引并行任务
- `parallelReduce`：创建并行归约任务
- `precede`：建立显式依赖
- `succeed`：可选对称接口
- `clear`：清空构图
- `dispatch`：提交执行
- `dispatchAndWait`：同步执行辅助接口

### 说明

`parallelReduce` 很值得正式纳入设计，因为游戏引擎里经常会有：

- visibility / culling 统计
- jobs result 汇总
- broad phase 统计
- streaming bytes 汇总
- profiler counters 聚合

它不是“锦上添花”，而是常用能力。

## 3.2 动态子图能力

建议增加：

- `subgraphTask`
- `dynamicTask`
- `expandTask`

### 用途

用于处理执行期才知道展开内容的场景，例如：

- streaming 资源树展开
- world partition 任务扩展
- AI / gameplay 事件生成后续任务
- scene load 过程中按资源依赖生成子任务

### 设计原则

动态子图必须被正式纳入抽象层，因为这正是 taskflow 的强项之一，将来自研库也应该支持甚至增强。

## 3.3 条件与分支能力

建议增加：

- `conditionTask`
- `branchTask`
- `switchTask`

### 用途

- LOD 分支
- 资源状态分支
- fallback path
- 启用 / 禁用某系统组
- pipeline 某阶段可跳过

这种能力如果不纳入设计，业务层最终还是会退化为“任务里写 if 然后绕过调度图”，可观测性会变差。

## 3.4 pipeline / 流水线能力

建议增加：

- `pipeline`
- `serialStage`
- `parallelStage`

### 适用场景

- 资源导入
- 关卡流送
- 网络包处理
- IO -> decode -> upload -> finalize
- asset build pipeline

对于引擎来说，pipeline 是一等公民，不应该完全依赖业务代码自己拼 DAG。

## 3.5 外部异步完成能力

建议增加：

- `externalTask`
- `completionSource`
- `resumeHandle`
- `detachedTask`

### 用途

- IO 完成回调接回任务图
- GPU fence 回流
- 网络异步返回
- 平台 SDK 回调接入
- 文件监控/热重载通知

### 设计意义

很多引擎任务不是“在 worker 上一直算完”，而是：

1. 发起异步请求
2. 当前逻辑挂起/让出
3. 外部事件完成后恢复后续图

ThreadCenter 如果不支持这类能力，就很难接真实引擎子系统。

## 3.6 主线程 / 渲染线程门控能力

建议把 gate 正式做成一类接口：

- `mainThreadGate`
- `renderThreadGate`
- `gpuCompletionGate`
- `customGate`

### 为什么必须做

游戏引擎天然有这些同步点：

- 主线程对象创建/销毁
- UI 树变更
- 渲染命令提交
- RHI / GPU 资源生命周期转换

### 设计重点

gate 不是单纯的 mutex/condition_variable 替代。

它应该是：

- 任务图中的正式节点
- 可被 profiler 看见
- 可被 frame budget 统计
- 可表达“谁在等待谁”

## 3.7 后台守护与长任务隔离能力

建议增加：

- `daemonTask`
- `longRunningTask`
- `blockingTask`
- `serviceLane`

### 用途

- 资源流送
- 后台预热
- shader compile
- 压缩/解压
- 导航网格构建
- 大型路径搜索

### 性能要求

这些任务绝不能简单跟帧内 tiny jobs 混跑，否则会造成：

- worker 被长期占据
- 帧内任务延迟变大
- stealing 效果变差
- frame jitter 明显

因此设计上必须支持 lane 隔离与 blocking compensation。

---

## 4. 高性能设计：调度模型该怎么做

## 4.1 任务分类必须进入正式模型

我建议 ThreadCenter 把任务明确分成以下几类：

### A. Tiny Compute Task

特征：

- 极短
- 高频
- 帧内大量存在
- 适合 work-stealing

例如：

- ECS chunk update
- animation pose update
- visibility chunk test

### B. Batch Parallel Task

特征：

- 索引型或块型并行
- 强调分块、归约、批次调度

例如：

- for each actor / component
- 粒子更新
- transform propagation

### C. Dynamic Expansion Task

特征：

- 执行期生成子图
- 结构不固定

例如：

- world partition 展开
- streaming 依赖树展开

### D. Blocking Task

特征：

- 可能阻塞
- 不适合占用普通 frame worker

例如：

- 文件 IO
- 外部 SDK 调用
- 同步等待平台接口

### E. External Async Task

特征：

- 发起后等待外部完成
- 需要 resume 机制

例如：

- GPU fence
- socket completion
- async file request

### F. Gate Task

特征：

- 表达线程边界或同步边界
- 是任务图里可观测的控制节点

例如：

- main thread gate
- render thread gate
- scene commit gate

ThreadCenter 的调度器必须先认识任务类型，才能谈高性能。

## 4.2 Domain Lane 设计

建议引入 **执行域（domain lane）** 概念。

至少预设这些 lane：

- `FRAME_CRITICAL`
- `FRAME_NORMAL`
- `BACKGROUND`
- `STREAMING`
- `BLOCKING`
- `RENDER_ASSIST`
- `SERVICE`

### 说明

这不是要求后端一定有物理独立线程池，而是逻辑上必须区分：

- 谁能抢占谁
- 谁能偷谁的任务
- 谁不能跟谁混跑
- 谁受 frame budget 管理

### 当前 taskflow 后端建议

在 taskflow 后端下可以先做“逻辑 lane + hint + compensation 策略”，以后你的自研库再把它们做成更真实的队列或 worker class。

## 4.3 Priority 不只是元信息，要进入调度决策

`TaskPriority` 不应该只停留在 `TaskDesc` 里。

建议正式定义 priority 语义：

- `CRITICAL`：帧关键路径、gate 前任务、主线程等待链路
- `HIGH`：帧内高优先级计算任务
- `NORMAL`：默认任务
- `LOW`：可延迟任务、预热任务、非关键后台任务

### 建议规则

- `CRITICAL` 不允许被 `LOW` 拖住
- `LOW` 可被 budget 削减或延后
- `HIGH` 在帧 budget 紧张时优先于 `NORMAL`
- `BACKGROUND` + `LOW` 可整体让位于 `FRAME_CRITICAL`

## 4.4 Blocking Compensation

高性能线程中心必须考虑：

> 某些 worker 被阻塞时，系统是否需要补充并发度。

建议设计中明确支持：

- 标记型 blocking task
- 动态 compensation worker
- 阻塞期间 worker 降级/隔离
- 超时阻塞告警

### 为什么重要

如果一个 task 标记了 `LONG_RUNNING` 或 `BLOCKING`，但仍占着普通帧 worker 不放，那么：

- frame latency 会不稳定
- stealing 会失衡
- CPU 利用率可能看起来高，但帧时间会变差

## 4.5 图编译与复用

要高性能，不能每帧都把完整图当成“临时脚本”乱建。

建议引入两层计划：

### A. Static Compiled Plan

适用于：

- 固定的帧阶段结构
- 固定系统依赖
- 固定 gate/buffer

特点：

- 一次编译，多帧复用
- 节点索引稳定
- 适合做 trace 和 profile 聚合

### B. Dynamic Overlay Plan

适用于：

- 本帧临时追加任务
- streaming 扩展任务
- 场景变化产生的临时分支

特点：

- 增量式附加
- 可跟 static plan 组合提交

### 最终建议

ThreadCenter 最好具备：

- `CompiledFramePlan`
- `TransientPlan`
- `FrameInstance`

这样静态部分复用，动态部分附着，性能和灵活性兼顾。

---

## 5. 面向游戏引擎的完整功能面

## 5.1 FramePlan：引擎核心 DSL

建议新增 `FramePlan`，比通用 `Plan` 更高层。

推荐能力：

- `addPhase`
- `addSystem`
- `addBarrier`
- `addGate`
- `compile`
- `instantiate`
- `dispatchFrame`

### 推荐预设 phase

至少考虑这些：

- `INPUT`
- `EARLY_UPDATE`
- `GAMEPLAY`
- `PHYSICS`
- `ANIMATION`
- `AI`
- `CULLING`
- `RENDER_PREP`
- `RENDER_SUBMIT`
- `STREAMING_FINALIZE`
- `END_FRAME`

### SystemGroup 语义

每个系统除了 task body 以外，还应该声明：

- phase
- read/write resource set
- priority
- domain lane
- 是否可并行
- 是否需要主线程 gate
- 是否允许 budget 缩减

这样未来就能进一步做：

- 自动依赖连边
- 资源冲突检测
- phase 内排序
- critical path 分析

## 5.2 资源与冲突建模

建议在设计层预留：

- `ResourceTag`
- `ReadSet`
- `WriteSet`
- `ConflictPolicy`

### 为什么有必要

游戏引擎里很多串行依赖其实来自资源冲突，而不是逻辑顺序。

如果 ThreadCenter 将来能识别：

- 谁读 transform graph
- 谁写 transform graph
- 谁写 render list
- 谁读 streaming state

那么很多连边可以自动生成或至少自动验证。

即使第一版不自动调度，也建议先把接口预留出来。

## 5.3 取消与生命周期控制

建议正式加入：

- `CancelSource`
- `CancelToken`
- `RunScope`
- `SceneScope`
- `FrameScope`

### 建议语义

- frame 结束自动取消过期 frame token
- scene unload 取消所有 scene-bound task
- engine shutdown 触发全局 cancel
- background task 可声明 cancel safe / unsafe

### 要求

取消必须是协作式的，不要做粗暴 thread kill。

## 5.4 异常与故障策略

建议给 `Center` 增加统一 fault policy：

- `Panic`
- `CaptureAndStop`
- `CaptureAndContinue`
- `ForwardToMainThread`

### 适用场景

- 开发版：尽量捕获并转发到调试系统
- 发行版：记录并降级
- 工具链模式：允许继续处理更多任务并收集诊断

## 5.5 Profiling / Trace

建议加入：

- `TraceHooks`
- `TaskEvent`
- `WorkerEvent`
- `FrameTrace`
- `CriticalPathReport`

### 最低需要的事件

- `onPlanBuild`
- `onTaskReady`
- `onTaskStart`
- `onTaskFinish`
- `onTaskCancel`
- `onTaskBlock`
- `onTaskResume`
- `onGateEnter`
- `onGateLeave`
- `onDispatchBegin`
- `onDispatchEnd`

### 结果

有了这些才能接：

- Tracy / Optick / 自研 profiler
- frame analysis HUD
- worker utilization 图
- stall diagnosis 报告

---

## 6. 针对 taskflow 的“增强型设计利用”

既然当前后端是 taskflow，而且你明确不需要为了多后端牺牲 taskflow 强功能，那么我建议把以下能力明确吸收进 ThreadCenter 设计。

## 6.1 Subflow / 动态子图能力

这是 taskflow 的关键强项之一。

ThreadCenter 应把它提升为正式能力：

- 不是裸暴露 `tf::Subflow`
- 而是暴露 `DynamicTaskBuilder` / `SubgraphContext` 这种引擎语义对象

### 好处

- 业务仍然不直接依赖 taskflow
- 但动态任务展开能力被保留
- 将来自研库也可直接做得更强

## 6.2 Condition Task / 分支图

这类能力对于：

- 多阶段 fallback
- asset 状态分支
- streaming 决策
- 平台差异路径

都很有价值。

建议 ThreadCenter 提供 branch/switch 语义，而不是让业务层自己把整个条件逻辑塞进一个黑盒 lambda。

## 6.3 Pipeline

taskflow 的 pipeline 能力值得吸收，因为很多引擎后台流程天然是 token/stage 模型。

建议 ThreadCenter 中把 pipeline 设计成独立能力面，而不是仅仅当作 Plan 的某种特殊写法。

## 6.4 Async / Silent Async / 外部完成桥接

taskflow 在异步任务桥接方面也有价值。

ThreadCenter 设计里建议明确支持：

- 向线程中心提交 detached async work
- 从外部事件 resume 回图
- 允许某些任务脱离 frame graph 独立运行

## 6.5 Observer / 执行观测

只要 taskflow 后端能提供可利用的观察点，ThreadCenter 就应该统一转成：

- task event
- worker event
- timeline event

不要等换自研库时再设计 profiler 接口。

---

## 7. 关键数据结构设计建议

为了高性能，我建议后续结构设计往这个方向走。

## 7.1 TaskHandle 不应长期依赖后端原生句柄语义

虽然当前骨架里 `TaskHandle` 直接包 native node，但从长期设计上，更推荐逐步转向：

- `TaskId`
- `NodeIndex`
- `PlanLocalHandle`

### 原因

- 更容易做 plan compile/cache
- 更容易做 trace/stats 索引
- 更容易跨后端保持稳定标识
- 更容易支持 plan serialization / debugging dump

### 建议

对外仍叫 `TaskHandle`，但内部逐渐转为稳定整数句柄或轻量索引。

## 7.2 TaskDesc 建议扩展字段

目前已有：

- `name`
- `domain`
- `priority`
- `flags`
- `affinity_mask`
- `stack_hint_kb`

建议未来扩展到：

- `task_kind`
- `lane`
- `budget_class`
- `estimated_cost_ns`
- `min_grain_size`
- `max_concurrency`
- `oversubscribe_policy`
- `cancel_behavior`
- `resource_read_mask`
- `resource_write_mask`
- `trace_color`
- `debug_tag`

### 说明

这些字段未必第一阶段全部生效，但建议先把接口思路设计完整。

## 7.3 Plan 构建内存策略

为了避免构图开销成为瓶颈，建议：

- `Plan` 使用 arena / PMR
- 节点、边、元信息集中分配
- frame-local plan 支持 reset 而不是逐个销毁
- 名字建议转 `NameId` 或 interned string，而不是热路径动态复制

## 7.4 Name 与 Trace 标识分离

建议区分：

- `display_name`：调试/trace 展示
- `stable_name_id`：统计聚合键

这样可以做到：

- trace 里有人类可读字符串
- profiler 聚合不依赖运行期字符串比较

---

## 8. 建议的公共 API 轮廓

下面给出一个建议方向，重点是语义，不是最终语法。

```cpp
namespace ThreadCenter {

struct TaskDesc;
struct CancelToken;
struct TraceHooks;

class Center;
class Plan;
class FramePlan;
class CompiledFramePlan;
class FrameInstance;
class TaskHandle;
class Barrier;
class Gate;
class CompletionSource;

class Center final {
public:
  auto makePlan() -> Plan;
  auto makeFramePlan() -> FramePlan;

  auto dispatch(Plan& plan_) -> RunHandle;
  auto dispatchFrame(FrameInstance& frame_) -> RunHandle;

  void waitIdle();
  void setTraceHooks(const TraceHooks& hooks_);
  void requestStop();
};

class Plan final {
public:
  template <class Fn>
  auto task(const TaskDesc& desc_, Fn&& fn_) -> TaskHandle;

  template <class Fn>
  auto dynamicTask(const TaskDesc& desc_, Fn&& fn_) -> TaskHandle;

  template <class Fn>
  auto conditionTask(const TaskDesc& desc_, Fn&& fn_) -> TaskHandle;

  template <class Index, class Fn>
  auto parallelFor(const TaskDesc& desc_, Index first_, Index last_, Index step_, Fn&& fn_)
    -> TaskHandle;

  template <class Range, class Body, class Join>
  auto parallelReduce(const TaskDesc& desc_, Range&& range_, Body&& body_, Join&& join_)
    -> TaskHandle;

  auto mainThreadGate(const TaskDesc& desc_) -> Gate;
  auto renderThreadGate(const TaskDesc& desc_) -> Gate;
  auto pipeline(const TaskDesc& desc_) -> PipelineHandle;

  void precede(TaskHandle from_, TaskHandle to_);
  void precede(TaskHandle from_, Gate to_);
  void precede(Gate from_, TaskHandle to_);
};

class FramePlan final {
public:
  auto addPhase(FramePhase phase_) -> PhaseHandle;
  auto addBarrier(std::string_view name_) -> Barrier;
  auto addSystem(const SystemDesc& desc_) -> SystemHandle;
  auto compile() -> CompiledFramePlan;
};

}
```

这个方向的关键点是：

- 保留 DAG 基础能力
- 正式支持动态子图 / 条件 / pipeline / gate
- 引入 frame-level DSL
- 不暴露后端类型

---

## 9. 任务提交流程建议

建议整个运行时按这条链路组织：

1. **系统注册期**
   - 注册 phase / system / barrier / gate / resource tag

2. **编译期**
   - 生成 `CompiledFramePlan`
   - 分析静态依赖
   - 生成 stable task id
   - 建立 trace/profiler 元数据

3. **帧实例化**
   - 绑定当前 frame context
   - 绑定 cancel token
   - 挂接本帧动态 overlay task

4. **调度提交**
   - 把 frame critical / background / blocking 分类落到对应策略
   - 提交给 backend runtime

5. **执行期间**
   - 动态子图展开
   - 外部完成恢复
   - gate rendezvous
   - trace 采样

6. **帧结束**
   - 回收 transient 资源
   - 汇总 profiling counters
   - 生成 critical path / stall 报告

---

## 10. 性能重点：必须防住的几个坑

## 10.1 小任务过碎

如果没有 grain / batch policy，图里会出现大量过小任务，导致：

- 构图成本高
- 调度开销高
- stealing 开销高
- trace 数据噪音高

### 建议

给 `parallelFor` / `parallelReduce` 增加：

- `grain_size`
- `min_batch`
- `partition_policy`

## 10.2 阻塞污染帧 worker

如果 blocking task 跟 frame critical job 混跑，会直接造成帧抖动。

### 建议

正式定义：

- `TaskFlags::LONG_RUNNING`
- `TaskFlags::BLOCKING`（建议未来新增）
- lane 隔离
- compensation 策略

## 10.3 任务名频繁分配

trace / profiler 常见坑之一是每次建图都动态分配大量字符串。

### 建议

- 运行期内部尽量用 `NameId`
- 仅在 debug/trace 需要时映射回字符串
- 静态计划名称做驻留/intern

## 10.4 每帧从零建整图

如果帧结构大部分固定，每帧完整重建会浪费很多 CPU。

### 建议

- static compiled frame graph
- dynamic overlay
- frame-local reset allocator

## 10.5 观测点过重

profiling 如果侵入过重，会反过来拖慢调度器。

### 建议

- trace hook 分层：off / counters / lightweight timeline / full debug
- 编译期开关
- 运行期开关
- 大事件批量写 ring buffer

---

## 11. 建议的阶段性演进路线

如果继续完善，我建议按下面顺序推进。

## 第一阶段：把基础能力补齐

目标：让 ThreadCenter 从骨架变成“像样的模块”。

优先项：

1. `parallelReduce`
2. `TaskKind` / `lane` / `budget_class`
3. `CancelToken`
4. `TraceHooks`
5. `mainThreadGate`
6. `blocking / long-running policy`
7. `Plan` arena 化

## 第二阶段：引入 frame 级 DSL

目标：让它真正服务游戏主循环。

优先项：

1. `FramePlan`
2. `FramePhase`
3. `SystemDesc`
4. `Barrier`
5. `Gate`
6. `CompiledFramePlan`
7. `FrameInstance`

## 第三阶段：深入 taskflow 强能力接入

目标：不浪费 taskflow 当前价值。

优先项：

1. dynamic subgraph
2. condition task
3. pipeline
4. external completion bridge
5. observer -> trace bridge

## 第四阶段：性能与诊断深化

目标：让它能在大项目里定位问题并稳定扩展。

优先项：

1. critical path analysis
2. budget overrun report
3. worker utilization view
4. long task detector
5. graph dump / debug visualization

---

## 12. 最终结论

这版“完善设计”的核心结论可以总结为三句话：

### 结论一

ThreadCenter 应该被设计成：

> **引擎统一并发控制中心，而不是 taskflow 的轻包装。**

### 结论二

因为未来你的自研线程库只会更强，所以当前设计不必为了“多后端最低公分母”而压缩能力面。

因此：

> **dynamic subgraph、condition、pipeline、external completion、gate、trace 这些 taskflow 强项，应当进入正式设计。**

### 结论三

高性能的关键不只是“跑在线程池上”，而是：

- 任务分类
- lane 隔离
- blocking compensation
- 图编译复用
- gate 建模
- 低开销观测

也就是说：

> **真正的高性能来自调度模型和执行策略，而不只是某个后端库本身。**

---

## 13. 建议的下一步实现顺序

如果按照“投入产出比”排序，我建议后续实现顺序是：

1. **扩展 `TaskDesc` 与任务分类模型**
2. **加入 `CancelToken` 与 `TraceHooks`**
3. **加入 `mainThreadGate` / `Gate` 抽象**
4. **补 `parallelReduce`**
5. **设计 `FramePlan` / `CompiledFramePlan`**
6. **再接 dynamic subgraph / condition / pipeline**

这个顺序比较稳：

- 先把基础调度语义站稳
- 再补生命周期和观测
- 再做 frame DSL
- 最后放大 taskflow 的高级能力
