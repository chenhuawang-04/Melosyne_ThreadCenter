cmake_minimum_required(VERSION 3.24)

project(ThreadCenter
  VERSION 0.1.0
  LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(THREAD_CENTER_BUILD_EXAMPLES "Build ThreadCenter examples" ON)
option(THREAD_CENTER_BUILD_TESTS "Build ThreadCenter tests" ON)
option(THREAD_CENTER_BUILD_BENCHMARKS "Build ThreadCenter benchmarks" ON)

add_library(ThreadCenter INTERFACE)
add_library(ThreadCenter::ThreadCenter ALIAS ThreadCenter)

target_compile_features(ThreadCenter INTERFACE cxx_std_23)
target_include_directories(ThreadCenter INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/taskflow-4.0.0>"
)

if(THREAD_CENTER_BUILD_EXAMPLES)
  add_executable(thread_center_basic_example
    examples/basic_usage.cpp
  )
  target_link_libraries(thread_center_basic_example PRIVATE ThreadCenter::ThreadCenter)
endif()

if(THREAD_CENTER_BUILD_TESTS)
  enable_testing()

  add_executable(thread_center_tests
    tests/thread_center_tests.cpp
  )
  target_link_libraries(thread_center_tests PRIVATE ThreadCenter::ThreadCenter)

  add_test(
    NAME thread_center_tests
    COMMAND thread_center_tests
  )
endif()

if(THREAD_CENTER_BUILD_BENCHMARKS)
  add_executable(thread_center_vs_taskflow_bench
    benchmarks/thread_center_vs_taskflow_bench.cpp
  )
  target_link_libraries(thread_center_vs_taskflow_bench PRIVATE ThreadCenter::ThreadCenter)
endif()
