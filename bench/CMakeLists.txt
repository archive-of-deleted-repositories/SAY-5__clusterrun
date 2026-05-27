add_library(clusterrun_bench_common bench_common.cpp)
target_link_libraries(clusterrun_bench_common PUBLIC clusterrun_core)
target_include_directories(clusterrun_bench_common PUBLIC ${CMAKE_SOURCE_DIR})

add_executable(bench_throughput bench_throughput.cpp)
target_link_libraries(bench_throughput PRIVATE clusterrun_bench_common clusterrun_controller_lib
                      clusterrun_worker_lib)

add_executable(bench_fault_recovery bench_fault_recovery.cpp)
target_link_libraries(bench_fault_recovery PRIVATE clusterrun_bench_common
                      clusterrun_controller_lib clusterrun_worker_lib)

# `make bench-regress` runs the throughput bench and fails if jobs/sec drops more
# than the allowed fraction below the checked-in baseline (bench/baseline.txt).
add_custom_target(bench-regress
  COMMAND ${CMAKE_COMMAND}
    -DBENCH=$<TARGET_FILE:bench_throughput>
    -DBASELINE=${CMAKE_SOURCE_DIR}/bench/baseline.txt
    -DTOLERANCE=0.30
    -P ${CMAKE_SOURCE_DIR}/bench/regress.cmake
  DEPENDS bench_throughput
  USES_TERMINAL)
