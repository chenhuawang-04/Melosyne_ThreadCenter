module;

// Keep system/third-party dependencies on #include path for Clang compatibility.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
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

export module Center.Thread;

#define CENTER_THREAD_MODULE_BUILD 1
export {
#include "thread_center/thread_center.hpp"
}
#undef CENTER_THREAD_MODULE_BUILD
