cmake_minimum_required(VERSION 3.28)

project(ThreadCenter
  VERSION 0.1.0
  LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(THREAD_CENTER_BUILD_EXAMPLES "Build ThreadCenter examples" ON)
option(THREAD_CENTER_BUILD_TESTS "Build ThreadCenter tests" ON)
option(THREAD_CENTER_BUILD_BENCHMARKS "Build ThreadCenter benchmarks" ON)
option(THREAD_CENTER_BUILD_HEADER_API "Build header-only ThreadCenter API target" ON)
option(THREAD_CENTER_BUILD_MODULES "Build C++23 module ThreadCenter API target" ON)
option(THREAD_CENTER_BUILD_TESTS_HEADER "Build header-api test binary" ON)
option(THREAD_CENTER_BUILD_TESTS_MODULE "Build module-api test binary" ON)
option(THREAD_CENTER_BUILD_BENCH_HEADER "Build header-api benchmark binary" ON)
option(THREAD_CENTER_BUILD_BENCH_MODULE "Build module-api benchmark binary" ON)
option(THREAD_CENTER_PERF_FORCE_O3 "Force O3/Max speed flags for benchmark targets" ON)
option(THREAD_CENTER_PERF_ENABLE_NATIVE "Enable native CPU tune flags for benchmark targets" OFF)
option(THREAD_CENTER_PERF_ENABLE_LTO "Enable IPO/LTO for benchmark targets" OFF)

set(THREAD_CENTER_PUBLIC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(THREAD_CENTER_TASKFLOW_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/taskflow-4.0.0")

if(THREAD_CENTER_BUILD_HEADER_API)
  add_library(ThreadCenterHeaders INTERFACE)
  add_library(ThreadCenter::ThreadCenterHeaders ALIAS ThreadCenterHeaders)
  # Backward-compatible alias kept as default header API entry.
  add_library(ThreadCenter::ThreadCenter ALIAS ThreadCenterHeaders)

  target_compile_features(ThreadCenterHeaders INTERFACE cxx_std_23)
  target_include_directories(ThreadCenterHeaders INTERFACE
  "$<BUILD_INTERFACE:${THREAD_CENTER_PUBLIC_INCLUDE_DIR}>"
  "$<INSTALL_INTERFACE:include>"
  "$<BUILD_INTERFACE:${THREAD_CENTER_TASKFLOW_INCLUDE_DIR}>"
  )
endif()

if(THREAD_CENTER_BUILD_MODULES)
  add_library(ThreadCenterModules STATIC)
  add_library(ThreadCenter::ThreadCenterModules ALIAS ThreadCenterModules)

  target_compile_features(ThreadCenterModules PUBLIC cxx_std_23)
  target_include_directories(ThreadCenterModules PUBLIC
    "${THREAD_CENTER_PUBLIC_INCLUDE_DIR}"
    "${THREAD_CENTER_TASKFLOW_INCLUDE_DIR}"
  )

  target_sources(ThreadCenterModules
    PUBLIC
      FILE_SET CXX_MODULES FILES
        modules/Center.Thread.Common.cppm
        modules/Center.Thread.Backend.Taskflow.cppm
        modules/Center.Thread.Plan.cppm
        modules/Center.Thread.FramePlan.cppm
        modules/Center.Thread.ProfilingExport.cppm
        modules/Center.Thread.cppm
  )
endif()

if(THREAD_CENTER_BUILD_TESTS_MODULE AND NOT THREAD_CENTER_BUILD_MODULES)
  message(FATAL_ERROR "THREAD_CENTER_BUILD_TESTS_MODULE=ON requires THREAD_CENTER_BUILD_MODULES=ON")
endif()

if(THREAD_CENTER_BUILD_BENCH_MODULE AND NOT THREAD_CENTER_BUILD_MODULES)
  message(FATAL_ERROR "THREAD_CENTER_BUILD_BENCH_MODULE=ON requires THREAD_CENTER_BUILD_MODULES=ON")
endif()

if(THREAD_CENTER_BUILD_TESTS_HEADER AND NOT THREAD_CENTER_BUILD_HEADER_API)
  message(FATAL_ERROR "THREAD_CENTER_BUILD_TESTS_HEADER=ON requires THREAD_CENTER_BUILD_HEADER_API=ON")
endif()

if(THREAD_CENTER_BUILD_BENCH_HEADER AND NOT THREAD_CENTER_BUILD_HEADER_API)
  message(FATAL_ERROR "THREAD_CENTER_BUILD_BENCH_HEADER=ON requires THREAD_CENTER_BUILD_HEADER_API=ON")
endif()

function(thread_center_add_runner target_name source_file use_modules)
  add_executable(${target_name} "${source_file}")
  target_compile_features(${target_name} PRIVATE cxx_std_23)

  if(${use_modules})
    target_compile_definitions(${target_name} PRIVATE THREAD_CENTER_USE_MODULES=1)
    target_link_libraries(${target_name} PRIVATE ThreadCenter::ThreadCenterModules)
  else()
    target_link_libraries(${target_name} PRIVATE ThreadCenter::ThreadCenterHeaders)
  endif()
endfunction()

function(thread_center_apply_perf_flags target_name)
  if(THREAD_CENTER_PERF_FORCE_O3)
    if(MSVC)
      target_compile_options(${target_name} PRIVATE /O2 /Ob3)
    else()
      target_compile_options(${target_name} PRIVATE -O3)
    endif()
  endif()

  if(THREAD_CENTER_PERF_ENABLE_NATIVE)
    if(MSVC)
      target_compile_options(${target_name} PRIVATE /arch:AVX2)
    else()
      target_compile_options(${target_name} PRIVATE -march=native -mtune=native)
    endif()
  endif()
endfunction()

if(THREAD_CENTER_BUILD_EXAMPLES)
  if(THREAD_CENTER_BUILD_HEADER_API)
    add_executable(thread_center_basic_example
      examples/basic_usage.cpp
    )
    target_link_libraries(thread_center_basic_example PRIVATE ThreadCenter::ThreadCenterHeaders)
  else()
    message(STATUS "Skip thread_center_basic_example: header API disabled.")
  endif()
endif()

if(THREAD_CENTER_BUILD_TESTS)
  enable_testing()

  if(THREAD_CENTER_BUILD_TESTS_HEADER)
    thread_center_add_runner(thread_center_tests tests/thread_center_tests.cpp FALSE)
    add_test(
      NAME thread_center_tests
      COMMAND thread_center_tests
    )
  endif()

  if(THREAD_CENTER_BUILD_TESTS_MODULE)
    thread_center_add_runner(thread_center_tests_module tests/thread_center_tests.cpp TRUE)
    add_test(
      NAME thread_center_tests_module
      COMMAND thread_center_tests_module
    )
  endif()
endif()

if(THREAD_CENTER_BUILD_BENCHMARKS)
  if(THREAD_CENTER_BUILD_BENCH_HEADER)
    thread_center_add_runner(thread_center_vs_taskflow_bench
      benchmarks/thread_center_vs_taskflow_bench.cpp FALSE)
    thread_center_apply_perf_flags(thread_center_vs_taskflow_bench)
  endif()

  if(THREAD_CENTER_BUILD_BENCH_MODULE)
    thread_center_add_runner(thread_center_vs_taskflow_bench_module
      benchmarks/thread_center_vs_taskflow_bench.cpp TRUE)
    thread_center_apply_perf_flags(thread_center_vs_taskflow_bench_module)
    if(THREAD_CENTER_BUILD_MODULES)
      thread_center_apply_perf_flags(ThreadCenterModules)
    endif()
  endif()
endif()

if(THREAD_CENTER_PERF_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT thread_center_ipo_supported OUTPUT thread_center_ipo_output)
  if(thread_center_ipo_supported)
    if(THREAD_CENTER_BUILD_BENCH_HEADER)
      set_property(TARGET thread_center_vs_taskflow_bench PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
    if(THREAD_CENTER_BUILD_BENCH_MODULE)
      set_property(TARGET thread_center_vs_taskflow_bench_module PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
    if(THREAD_CENTER_BUILD_MODULES)
      set_property(TARGET ThreadCenterModules PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
  else()
    message(WARNING "THREAD_CENTER_PERF_ENABLE_LTO=ON but IPO/LTO unsupported: ${thread_center_ipo_output}")
  endif()
endif()
