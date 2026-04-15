# ThreadCenter 设计说明

## 1. 目标定位

ThreadCenter 不是“线程池封装”，而是引擎级并发控制面，负责：

- **规划**：定义任务图、阶段、依赖、批处理边界
- **调度**：根据域、优先级、标记、亲和性做策略选择
- **执行**：把图提交给后端执行器并回收结果

换句话说，它应该是：

> **游戏引擎所有并发行为的唯一入口**

---

## 2. 总体分层

建议最终分为 4 层：

### A. Engine API 层（稳定公共层）

给业务系统、引擎子系统使用的接口：

- `ThreadCenter::Center`
- `ThreadCenter::Plan`
- `ThreadCenter::TaskHandle`
- `ThreadCenter::RunHandle`
- `ThreadCenter::TaskDesc`

这一层必须稳定，不能跟着 taskflow 的接口变化。

### B. Scheduling Model 层（稳定语义层）

定义统一调度语义：

- `ScheduleDomain`
- `TaskPriority`
- `TaskFlags`
- worker hints / affinity / budget / frame phase

这是**你未来自研线程库最值钱的部分**，因为它抽象的是“引擎需求”，不是“第三方库能力”。

### C. Backend Adapter 层（替换层）

当前实现：

- `ThreadCenter::Detail::TaskflowBackend`

将来可以新增：

- `ThreadCenter::Detail::MySchedulerBackend`
- `ThreadCenter::Detail::FiberBackend`
- `ThreadCenter::Detail::JobSystemBackend`

### D. Runtime / Instrumentation 层（观测层）

后续补：

- trace hooks
- worker event hooks
- frame budget profiler
- queue latency / stall metrics
- long task detector

---

## 3. 为什么要用编译期后端绑定

不建议用统一虚接口：

```cpp
struct IBackend {
  virtual void submit(...) = 0;
};
```

原因：

1. 热路径引入不必要的间接调用
2. 破坏内联机会
3. 难以把任务创建做成零额外包装
4. 容易把“最低公共能力”设计得过弱

因此建议用：

```cpp
template <class Backend>
class BasicCenter;
```

然后在引擎配置层给出：

```cpp
using Center = BasicCenter<Detail::TaskflowBackend>;
```

这样业务代码完全不变，将来只替换 alias 即可。

---

## 4. 为什么任务元信息要先设计全

即使 taskflow 当前不能完全消费这些信息，也建议先把统一任务描述保留下来：

- `domain`
- `priority`
- `flags`
- `affinity_mask`
- `stack_hint_kb`

因为这些不是“多余字段”，而是：

> **未来调度器能力和引擎并发契约的预留位**

如果现在不进 API，未来再补会导致上层大规模改签名。

---

## 5. 建议的后续扩展方向

### 5.1 Frame Pipeline

建议引入：

- `FramePhase`
- `SystemGroup`
- `Barrier`

例如：

- input
- gameplay
- physics
- animation
- culling
- render_prep

这样可以从“手写 DAG”逐步进化到“声明式帧调度”。

### 5.2 主线程/渲染线程栅栏

对引擎非常重要，建议尽早放进抽象层：

- `MainThreadGate`
- `RenderThreadGate`
- `GpuCompletionGate`

### 5.3 可取消执行

建议每次 dispatch 都能携带：

- stop token
- frame token
- scene token

场景切换和热重载会非常实用。

### 5.4 观测埋点

建议让后端适配层支持统一 hook：

- on_submit
- on_start
- on_finish
- on_wait
- on_steal

这样切换后端时，profiling 系统不用重写。

---

## 6. 重要约束建议

建议在团队层面定下以下规则：

1. 业务代码禁止直接依赖 taskflow
2. 所有并发入口必须走 ThreadCenter
3. 不允许绕过 `TaskDesc` 直接裸投递匿名任务
4. DAG 依赖必须显式表达，不允许隐式共享状态同步
5. 阻塞型任务必须显式打 `TaskFlags::LONG_RUNNING`

这样 ThreadCenter 才能逐步从“封装库”进化成“调度中心”。

