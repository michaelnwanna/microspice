cmake_minimum_required(VERSION 3.20)

project(microspice
  VERSION 0.3.0
  DESCRIPTION "An educational SPICE-inspired circuit simulator"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(MS_BUILD_TESTING "Build tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

add_library(microspice INTERFACE)
add_library(microspice::core ALIAS microspice)
target_include_directories(microspice INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(microspice INTERFACE cxx_std_17)

add_executable(dc_walkthrough examples/dc_walkthrough.cpp)
target_link_libraries(dc_walkthrough PRIVATE microspice::core)
ms_set_warnings(dc_walkthrough)

add_executable(transient_walkthrough examples/transient_walkthrough.cpp)
target_link_libraries(transient_walkthrough PRIVATE microspice::core)
ms_set_warnings(transient_walkthrough)

add_executable(microspice_cli examples/microspice_cli.cpp)
target_link_libraries(microspice_cli PRIVATE microspice::core)
ms_set_warnings(microspice_cli)

if(MS_BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()