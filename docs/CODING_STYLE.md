# Coding Style

本项目统一采用以下 C++ 代码风格。该规范与仓库根目录下的 `.clang-format`、`.clang-tidy`、`.editorconfig`、`.clang-format-ignore` 保持一致。

---

## 1. 命名规范

### 1.1 命名空间

- 使用**首字母大写驼峰**。
- 示例：`ThreadCenter`、`ThreadCenter::Detail`

### 1.2 类型名

适用于：

- `class`
- `struct`
- `enum class`
- `using` 类型别名
- `concept`

规则：

- 使用**首字母大写驼峰**。

示例：

- `Center`
- `Plan`
- `TaskDesc`
- `ExecutorConfig`
- `TaskflowBackend`
- `BackendAdapter`

### 1.3 枚举值

- 使用**全大写蛇形**。

示例：

- `ScheduleDomain::FRAME`
- `ScheduleDomain::GAMEPLAY`
- `TaskPriority::CRITICAL`
- `TaskFlags::LONG_RUNNING`

### 1.4 函数名

适用于：

- 普通函数
- 成员函数
- 静态成员函数
- 工厂函数

规则：

- 使用**首字母小写驼峰**。

示例：

- `makePlan`
- `parallelFor`
- `waitIdle`
- `workerCount`
- `hasFlag`

### 1.5 变量名

适用于：

- 局部变量
- 普通成员变量
- 公共字段
- 临时变量

规则：

- 使用**首字母小写蛇形**。

示例：

- `frame_plan`
- `begin_task`
- `run_handle`
- `affinity_mask`
- `stack_hint_kb`

### 1.6 参数名

规则：

- 使用**小写蛇形**。
- 名称**末尾追加下划线 `_`**。

示例：

- `config_`
- `desc_`
- `graph_`
- `run_handle_`
- `first_`
- `last_`

### 1.7 私有成员

规则：

- 使用**小写蛇形**。
- 名称**末尾追加下划线 `_`**。

示例：

- `native_`
- `config_`

---

## 2. 头文件与依赖约束

### 2.1 公共并发接口唯一入口

业务层和引擎层只允许包含：

- `thread_center/thread_center.hpp`

不允许直接依赖：

- `taskflow/taskflow.hpp`
- `taskflow/algorithm/*`
- 任何未来后端的原生头文件

### 2.2 include 顺序

建议顺序：

1. 当前文件对应头
2. C++ 标准库头
3. 项目内头文件
4. 第三方头文件

如果某个文件没有“当前文件对应头”，则保持标准库头在前、项目头在后。

---

## 3. API 设计约束

### 3.1 对外接口优先稳定

新增功能时优先扩展：

- `ThreadCenter::Center`
- `ThreadCenter::Plan`
- `ThreadCenter::TaskHandle`
- `ThreadCenter::RunHandle`
- `ThreadCenter::TaskDesc`

不要把第三方后端能力直接泄漏到公共 API。

### 3.2 热路径避免运行时多态

热路径中默认避免：

- 虚函数分发
- `std::function`
- 为抽象而抽象的堆分配封装

优先选择：

- 模板
- `concept`
- 编译期后端绑定
- 直接转发 callable

### 3.3 调度语义优先于后端能力

公共层新增字段时，优先表达“引擎需要什么”，而不是“当前 taskflow 恰好支持什么”。

例如：

- `domain`
- `priority`
- `flags`
- `affinity_mask`
- `stack_hint_kb`

---

## 4. 格式规范

### 4.1 缩进与换行

- 使用空格缩进。
- 缩进宽度为 **2**。
- 每个文件以换行结束。
- 删除行尾多余空白。

### 4.2 列宽

- 建议列宽上限为 **100**。

### 4.3 大括号风格

- 类型、函数、命名空间的大括号换行。
- 控制语句保持同一行。

示例：

```cpp
class BasicCenter final {
public:
  void waitIdle() {
    Backend::waitIdle(native_);
  }
};

if (!desc_.name.empty()) {
  task_.name(std::string(desc_.name));
}
```

---

## 5. 工具使用

### 5.1 格式化

使用仓库根目录 `.clang-format`：

```powershell
clang-format -i include/thread_center/thread_center.hpp
clang-format -i include/thread_center/detail/taskflow_backend.hpp
clang-format -i examples/basic_usage.cpp
```

### 5.2 静态检查

使用仓库根目录 `.clang-tidy`：

```powershell
clang-tidy -p build examples/basic_usage.cpp
```

说明：

- `.clang-tidy` 已固化命名规则。
- 新增文件应在提交前至少做一次命名与可读性检查。

---

## 6. 提交前检查清单

提交前请至少确认：

- [ ] 新增类型名是否为大驼峰
- [ ] 新增函数名是否为小驼峰
- [ ] 新增变量名是否为小写蛇形
- [ ] 新增参数名是否为小写蛇形并以 `_` 结尾
- [ ] 新增枚举值是否为全大写蛇形
- [ ] 是否未直接向业务层暴露后端原生类型
- [ ] 是否已运行 `clang-format`
- [ ] 是否已检查 `.clang-tidy` 报告中的命名问题

---

## 7. 特别约束

### 7.1 不允许业务层绕过 ThreadCenter

不允许在业务代码中直接创建或传播：

- `tf::Executor`
- `tf::Taskflow`
- `tf::Task`
- 其他后端原生任务/执行器类型

### 7.2 后端适配层是实现细节

后端适配层位于：

- `ThreadCenter::Detail`

其职责是实现公共抽象，不是给业务侧直接调用。

### 7.3 文档、示例、代码必须同步命名

只要公共 API 发生命名变化，必须同步更新：

- `examples/`
- `README.md`
- `docs/`

避免出现“代码风格已更新、文档仍是旧名字”的漂移。


