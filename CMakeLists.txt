cmake_minimum_required(VERSION 3.20)
project(clusterrun LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(CLUSTERRUN_BUILD_TESTS "Build unit and integration tests" ON)
option(CLUSTERRUN_BUILD_BENCH "Build benchmarks" ON)
option(CLUSTERRUN_BUILD_FUZZ "Build libFuzzer targets" OFF)
option(CLUSTERRUN_ASAN "Enable AddressSanitizer + UBSan" OFF)
option(CLUSTERRUN_TSAN "Enable ThreadSanitizer" OFF)
option(CLUSTERRUN_COVERAGE "Enable coverage instrumentation" OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

if(CLUSTERRUN_ASAN)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

if(CLUSTERRUN_TSAN)
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=thread)
endif()

if(CLUSTERRUN_COVERAGE)
  add_compile_options(--coverage -O0 -g)
  add_link_options(--coverage)
endif()

find_package(CURL REQUIRED)

# nlohmann_json: prefer installed package, fall back to header search.
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
  find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp)
  if(NOT NLOHMANN_JSON_INCLUDE_DIR)
    message(FATAL_ERROR "nlohmann/json.hpp not found")
  endif()
  add_library(nlohmann_json INTERFACE)
  target_include_directories(nlohmann_json INTERFACE ${NLOHMANN_JSON_INCLUDE_DIR})
  add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
endif()

include_directories(${CMAKE_SOURCE_DIR}/src)

add_subdirectory(src)

if(CLUSTERRUN_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(CLUSTERRUN_BUILD_BENCH)
  add_subdirectory(bench)
endif()

if(CLUSTERRUN_BUILD_FUZZ)
  add_subdirectory(fuzz)
endif()
