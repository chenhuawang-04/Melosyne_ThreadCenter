# ThreadCenter

一个面向游戏引擎的高性能线程中心（规划 / 调度 / 执行一体化），当前后端为 **Taskflow**，接口层按“可替换后端”设计。

> 目标：在不牺牲工程能力（可观测、取消、帧图语义）的前提下，尽量逼近纯 Taskflow 的调度性能。

---

## 1. 项目定位

ThreadCenter 不是单纯的线程池封装，而是“**引擎任务编排中心**”：

- 统一任务抽象（`TaskDesc` / `FrameSystemDesc`）
- 统一构图 API（普通任务、条件任务、动态任务、并行循环、流水线）
- 帧级编排（`FramePlan` + phase/resource 依赖）
- 统一可观测（trace hooks / runtime report / profiling export）
- 统一取消语义（`CancelSource` / `CancelToken`）
- 后端可切换（当前仅 `TaskflowBackend`）

---

## 2. 当前状态（2026-04）

- ✅ 核心 API 可用，测试可运行
- ✅ benchmark 框架可对比 `ThreadCenter vs 纯 Taskflow`
- ✅ 已完成多轮热路径优化（部分场景接近或优于 Taskflow）
- ⚠️ 小任务高频场景（例如 chain / fanout / dynamic）仍存在抽象层固定开销
- ⚠️ 多后端“设计已到位”，但暂未进行后端验证（按当前规划可后续接入）

---

## 3. 目录结构

```txt
ThreadCenter/
├─ include/thread_center/
│  ├─ thread_center.hpp           # 总入口头
│  ├─ common.hpp                  # 公共类型、事件、取消、runtime collector
│  ├─ plan.hpp                    # Plan / Dynamic / Pipeline / Center 核心实现
│  ├─ frame_plan.hpp              # FramePlan、phase/resource 自动依赖
│  ├─ profiling_export.hpp        # profiling 导出
│  └─ detail/taskflow_backend.hpp # Taskflow 后端适配层
├─ tests/thread_center_tests.cpp
├─ benchmarks/thread_center_vs_taskflow_bench.cpp
├─ scripts/compare_header_vs_module.ps1
├─ examples/basic_usage.cpp
├─ modules/Center.Thread*.cppm   # cppm 模块实现
├─ taskflow-4.0.0/                # vendored taskflow
└─ CMakeLists.txt
```

---

## 4. 构建与运行

### 4.1 配置与构建（Header + Module 双通道）

```bash
cmake -S . -B build -G Ninja \
  -DTHREAD_CENTER_BUILD_TESTS=ON \
  -DTHREAD_CENTER_BUILD_BENCHMARKS=ON \
  -DTHREAD_CENTER_BUILD_TESTS_HEADER=ON \
  -DTHREAD_CENTER_BUILD_TESTS_MODULE=ON \
  -DTHREAD_CENTER_BUILD_BENCH_HEADER=ON \
  -DTHREAD_CENTER_BUILD_BENCH_MODULE=ON \
  -DTHREAD_CENTER_PERF_FORCE_O3=ON \
  -DTHREAD_CENTER_PERF_ENABLE_NATIVE=ON \
  -DTHREAD_CENTER_PERF_ENABLE_LTO=ON
cmake --build build -j
```

### 4.2 运行测试（两套 API）

```bash
ctest --test-dir build --output-on-failure
# thread_center_tests          (header API)
# thread_center_tests_module   (cppm API)
```

### 4.3 运行基准（两套 API）

```bash
./build/thread_center_vs_taskflow_bench.exe --workers 10 --warmup 20 --measure 80 --repeats 3 --json build/bench_header.json
./build/thread_center_vs_taskflow_bench_module.exe --workers 10 --warmup 20 --measure 80 --repeats 3 --json build/bench_module.json
```

可选参数：

- `--scenario <name_substring>`：只跑匹配场景
- `--csv <path>`：导出 CSV
- `--json <path>`：导出 JSON
- `--list-scenarios`：列出全部场景

### 4.4 一键对比驱动脚本（Header vs Module）

```powershell
powershell -ExecutionPolicy Bypass -File scripts/compare_header_vs_module.ps1 `
  -BuildDir build `
  -Warmup 20 -Measure 80 -Repeats 3 `
  -OuterRounds 6 -OrderMode ABBA `
  -Scenario ChainBuildRun,ParallelForBuildRun,DynamicBuildRun
```

输出目录：`<BuildDir>/compare_report_<timestamp>/`，包含：

- `bench_header.json/csv/log`
- `bench_module.json/csv/log`
- `compare_summary.json/csv`
- `tests_header.log` / `tests_module.log`（除非 `-SkipTests`）

关键参数（用于严格对比）：

- `-OuterRounds`：外层轮次（建议 >= 4）
- `-OrderMode ABBA`：header/module 交错顺序，降低时序偏置
- `-CoolDownMs`：轮间冷却时间，减少温度/频率漂移影响
- `-Scenario`：支持逗号分隔多个过滤关键词

推荐对比协议（严谨版）：

1. 使用同一构建目录、同一编译参数（建议开启 `THREAD_CENTER_PERF_ENABLE_LTO`）
2. `-OrderMode ABBA`，`-OuterRounds >= 6`
3. 每轮固定 `Warmup/Measure/Repeats`，不要中途改参数
4. 关注 `compare_summary` 中：
   - `tc_delta_percent`
   - `tc_significance`
   - `taskflow_baseline_drift_percent`（>5% 建议重跑）
   - `stability`（CV 标签）

---

## 5. 快速上手

```cpp
#include "thread_center/thread_center.hpp"

int main() {
  ThreadCenter::Center center({.workers = 8});
  auto plan = center.makePlan();

  auto a = plan.task({.name = "A"}, [] { /* work */ });
  auto b = plan.task({.name = "B"}, [] { /* work */ });
  plan.precede(a, b);

  auto run_handle = center.dispatch(plan);
  run_handle.wait();
}
```

---

## 6. 关键 API 指引

### 6.1 `Center`

- `makePlan()`：创建一次性计划图
- `makeFramePlan()`：创建帧图构建器
- `dispatch(plan)` / `dispatchFrame(frame_instance)`：提交执行
- `waitIdle()`：等待后端空闲
- `setTraceHooks(...)`：设置 tracing 回调

### 6.2 `Plan`

- `task(desc, fn)`：普通任务
- `conditionTask(desc, fn)`：条件任务（返回分支索引）
- `dynamicTask(desc, fn)`：动态子图
- `parallelFor(desc, first, last, step, fn)`：并行循环
- `pipelineTask(desc, token_count, build_fn)`：流水线任务
- `gate(desc)` / `mainThreadGate(desc)`：门任务
- `precede(from, to)`：依赖边

### 6.3 `FramePlan`

- 带 `FramePhase` 的任务构图
- `systemTask(...) / parallelForSystem(...)`：自动资源依赖解析
- `phaseBarrier(...)`：阶段屏障
- `compile()`：编译为可重复实例化的 `CompiledFramePlan`

### 6.4 取消与追踪

- `CancelSource` + `CancelToken`：协作式取消
- `TraceHooks`：任务/调度/流水线事件回调
- `runtimeReport()`：帧运行统计报告

---

## 7. 性能设计说明（当前实现）

当前已引入的性能策略：

1. hot path 分支门控（trace/cancel 未启用时减少额外逻辑）
2. `parallel_for` 批处理步长优化 + 批次跨度计算优化
3. `SUBMITTED` 事件仅在 task trace hook 启用时发射
4. Taskflow 任务命名默认关闭，需显式 `TaskFlags::BACKEND_NAME` 才写入后端

> 说明：为了跨后端统一语义，仍保留了通用封装层；在小任务高频场景会体现为固定抽象开销。

---

## 8. 与纯 Taskflow 对比的正确姿势

建议统一配置并多次运行后取统计值：

- 固定 `workers/warmup/measure/repeats`
- 至少跑 2~5 轮，比较 `mean + median + p95 + ratio`
- 重点看：
  - Chain/FanOut/Dynamic（调度和封装开销敏感）
  - ParallelFor/Pipeline（批处理和复用优化收益）

ratio 定义：

- `ratio = ThreadCenter_mean / Taskflow_mean`
- `< 1`：ThreadCenter 更快
- `≈ 1`：性能接近
- `> 1`：存在额外成本（通常在小任务场景）

---

## 9. 约束与规范

当前代码遵循以下命名约束：

- 类名、命名空间：首字母大写驼峰
- 变量名：首字母小写蛇形
- 函数名：首字母小写驼峰
- 参数名：蛇形并以 `_` 结尾
- 枚举值：全大写蛇形

---

## 10. 常见问题（FAQ）

### Q1：为什么不直接用纯 Taskflow？
A：ThreadCenter提供了帧阶段、资源依赖、统一取消、可观测、后端切换等“引擎平台能力”。纯 Taskflow 在这些方面需要额外自建层。

### Q2：为什么某些场景比纯 Taskflow 慢？
A：这是抽象层与平台能力带来的固定税，特别在“小任务高频”场景更明显。

### Q3：如何拿到更接近纯 Taskflow 的性能？
A：保持 trace/cancel/profiling 等高级能力按需开启；任务粒度不要过碎；对 parallel_for 使用合理 `min_grain_size`。

---

## 11. 后续建议路线

1. 继续压缩小任务热路径封装成本（direct path 进一步下沉）
2. FramePlan 元数据结构降复杂度（减少线性去重）
3. 多后端适配验证与对齐测试
4. 基准协议化（固定环境、亲和、A/B 交错、统计显著性）

---

## 12. License

当前仓库未单独声明许可证；如需开源发布，请补充 `LICENSE` 文件并明确第三方依赖许可证（含 Taskflow）。
