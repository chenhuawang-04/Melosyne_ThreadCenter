cmake_minimum_required(VERSION 3.28)

project(ThreadCenter
  VERSION 0.1.0
  LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(THREAD_CENTER_IS_TOP_LEVEL ${PROJECT_IS_TOP_LEVEL})

if(THREAD_CENTER_IS_TOP_LEVEL)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()
set(THREAD_CENTER_PUBLIC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(THREAD_CENTER_TASKFLOW_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/taskflow-4.0.0")
set(THREAD_CENTER_MODULE_SOURCES
  modules/Center.Thread.Common.cppm
  modules/Center.Thread.Backend.Taskflow.cppm
  modules/Center.Thread.Plan.cppm
  modules/Center.Thread.FramePlan.cppm
  modules/Center.Thread.ProfilingExport.cppm
  modules/Center.Thread.cppm
)

option(THREAD_CENTER_BUILD_HEADER_API "Build header API target (ThreadCenter::ThreadCenter)" ON)
option(THREAD_CENTER_BUILD_MODULES "Build C++23 module API target (ThreadCenter::ThreadCenterModules)" ON)

option(THREAD_CENTER_BUILD_EXAMPLES "Build ThreadCenter examples" ${THREAD_CENTER_IS_TOP_LEVEL})
option(THREAD_CENTER_BUILD_TESTS "Build ThreadCenter tests" ${THREAD_CENTER_IS_TOP_LEVEL})
option(THREAD_CENTER_BUILD_BENCHMARKS "Build ThreadCenter benchmarks" ${THREAD_CENTER_IS_TOP_LEVEL})
option(THREAD_CENTER_BUILD_TESTS_HEADER "Build header-api test binary" ${THREAD_CENTER_IS_TOP_LEVEL})
option(THREAD_CENTER_BUILD_TESTS_MODULE "Build module-api test binary" ${THREAD_CENTER_IS_TOP_LEVEL})
option(THREAD_CENTER_BUILD_BENCH_HEADER "Build header-api benchmark binary" ${THREAD_CENTER_IS_TOP_LEVEL})
option(THREAD_CENTER_BUILD_BENCH_MODULE "Build module-api benchmark binary" ${THREAD_CENTER_IS_TOP_LEVEL})

option(THREAD_CENTER_ENABLE_INSTALL "Enable install/export/package rules" ${THREAD_CENTER_IS_TOP_LEVEL})
option(THREAD_CENTER_PERF_FORCE_O3 "Force O3/max speed flags for benchmark targets" ON)
option(THREAD_CENTER_PERF_ENABLE_NATIVE "Enable native CPU tune flags for benchmark targets" OFF)
option(THREAD_CENTER_PERF_ENABLE_LTO "Enable IPO/LTO for benchmark targets" OFF)

if(NOT THREAD_CENTER_BUILD_HEADER_API AND NOT THREAD_CENTER_BUILD_MODULES)
  message(FATAL_ERROR "At least one API target must be enabled: THREAD_CENTER_BUILD_HEADER_API or THREAD_CENTER_BUILD_MODULES")
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

if(THREAD_CENTER_BUILD_HEADER_API)
  add_library(ThreadCenter INTERFACE)
  add_library(ThreadCenter::ThreadCenter ALIAS ThreadCenter)

  target_compile_features(ThreadCenter INTERFACE cxx_std_23)
  target_include_directories(ThreadCenter INTERFACE
    "$<BUILD_INTERFACE:${THREAD_CENTER_PUBLIC_INCLUDE_DIR}>"
    "$<BUILD_INTERFACE:${THREAD_CENTER_TASKFLOW_INCLUDE_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  )
endif()

if(THREAD_CENTER_BUILD_MODULES)
  add_library(ThreadCenterModules STATIC)
  add_library(ThreadCenter::ThreadCenterModules ALIAS ThreadCenterModules)

  target_compile_features(ThreadCenterModules PUBLIC cxx_std_23)
  target_include_directories(ThreadCenterModules PUBLIC
    "$<BUILD_INTERFACE:${THREAD_CENTER_PUBLIC_INCLUDE_DIR}>"
    "$<BUILD_INTERFACE:${THREAD_CENTER_TASKFLOW_INCLUDE_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  )

  if(THREAD_CENTER_BUILD_HEADER_API)
    target_link_libraries(ThreadCenterModules PUBLIC ThreadCenter::ThreadCenter)
  endif()

  target_sources(ThreadCenterModules
    PUBLIC
      FILE_SET CXX_MODULES FILES ${THREAD_CENTER_MODULE_SOURCES}
  )
endif()

function(thread_center_add_runner target_name source_file use_modules)
  add_executable(${target_name} "${source_file}")
  target_compile_features(${target_name} PRIVATE cxx_std_23)

  if(${use_modules})
    target_compile_definitions(${target_name} PRIVATE THREAD_CENTER_USE_MODULES=1)
    target_link_libraries(${target_name} PRIVATE ThreadCenter::ThreadCenterModules)
  else()
    target_link_libraries(${target_name} PRIVATE ThreadCenter::ThreadCenter)
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
    add_executable(thread_center_basic_example examples/basic_usage.cpp)
    target_link_libraries(thread_center_basic_example PRIVATE ThreadCenter::ThreadCenter)
  else()
    message(STATUS "Skip thread_center_basic_example: header API disabled.")
  endif()
endif()

if(THREAD_CENTER_BUILD_TESTS)
  enable_testing()

  if(THREAD_CENTER_BUILD_TESTS_HEADER)
    thread_center_add_runner(thread_center_tests tests/thread_center_tests.cpp FALSE)
    add_test(NAME thread_center_tests COMMAND thread_center_tests)
  endif()

  if(THREAD_CENTER_BUILD_TESTS_MODULE)
    thread_center_add_runner(thread_center_tests_module tests/thread_center_tests.cpp TRUE)
    add_test(NAME thread_center_tests_module COMMAND thread_center_tests_module)
  endif()
endif()

if(THREAD_CENTER_BUILD_BENCHMARKS)
  if(THREAD_CENTER_BUILD_BENCH_HEADER)
    thread_center_add_runner(thread_center_vs_taskflow_bench benchmarks/thread_center_vs_taskflow_bench.cpp FALSE)
    thread_center_apply_perf_flags(thread_center_vs_taskflow_bench)
  endif()

  if(THREAD_CENTER_BUILD_BENCH_MODULE)
    thread_center_add_runner(thread_center_vs_taskflow_bench_module benchmarks/thread_center_vs_taskflow_bench.cpp TRUE)
    thread_center_apply_perf_flags(thread_center_vs_taskflow_bench_module)
    thread_center_apply_perf_flags(ThreadCenterModules)
  endif()
endif()

if(THREAD_CENTER_PERF_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT thread_center_ipo_supported OUTPUT thread_center_ipo_output)
  if(thread_center_ipo_supported)
    if(TARGET thread_center_vs_taskflow_bench)
      set_property(TARGET thread_center_vs_taskflow_bench PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
    if(TARGET thread_center_vs_taskflow_bench_module)
      set_property(TARGET thread_center_vs_taskflow_bench_module PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
    if(TARGET ThreadCenterModules)
      set_property(TARGET ThreadCenterModules PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
  else()
    message(WARNING "THREAD_CENTER_PERF_ENABLE_LTO=ON but IPO/LTO unsupported: ${thread_center_ipo_output}")
  endif()
endif()

if(THREAD_CENTER_ENABLE_INSTALL)
  if(THREAD_CENTER_BUILD_HEADER_API)
    install(TARGETS ThreadCenter EXPORT ThreadCenterTargets)
  endif()

  if(THREAD_CENTER_BUILD_MODULES)
    install(TARGETS ThreadCenterModules
      EXPORT ThreadCenterTargets
      FILE_SET CXX_MODULES DESTINATION ${CMAKE_INSTALL_LIBDIR}/thread_center
    )
  endif()

  install(DIRECTORY "${THREAD_CENTER_PUBLIC_INCLUDE_DIR}/thread_center"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    PATTERN ".idea" EXCLUDE
  )
  install(DIRECTORY "${THREAD_CENTER_TASKFLOW_INCLUDE_DIR}/taskflow"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  )

  install(EXPORT ThreadCenterTargets
    FILE ThreadCenterTargets.cmake
    NAMESPACE ThreadCenter::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ThreadCenter"
  )

  configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ThreadCenterConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ThreadCenterConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ThreadCenter"
  )

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ThreadCenterConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
  )

  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ThreadCenterConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ThreadCenterConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ThreadCenter"
  )
endif()
