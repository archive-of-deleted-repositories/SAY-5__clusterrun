# Runs the throughput bench, parses jobs_per_sec, and compares it against the
# baseline. Fails if throughput regressed by more than TOLERANCE (a fraction
# like 0.30). Invoked by the `bench-regress` target with BENCH, BASELINE, and
# TOLERANCE passed in. CMake math is integer-only, so values are truncated to
# whole jobs/sec for the comparison, which is fine at the scale we measure.

execute_process(
  COMMAND ${BENCH}
  OUTPUT_VARIABLE bench_out
  RESULT_VARIABLE bench_rc)

message(STATUS "bench output:\n${bench_out}")
if(NOT bench_rc EQUAL 0)
  message(FATAL_ERROR "throughput bench did not complete all jobs (rc=${bench_rc})")
endif()

string(REGEX MATCH "jobs_per_sec=([0-9.]+)" _m "${bench_out}")
set(current "${CMAKE_MATCH_1}")
if(current STREQUAL "")
  message(FATAL_ERROR "could not parse jobs_per_sec from bench output")
endif()
string(REGEX REPLACE "\\..*" "" current_int "${current}")

if(NOT EXISTS "${BASELINE}")
  file(WRITE "${BASELINE}" "${current_int}\n")
  message(STATUS "no baseline found; wrote ${current_int} jobs/sec as the new baseline")
  return()
endif()

file(READ "${BASELINE}" baseline)
string(STRIP "${baseline}" baseline)
string(REGEX REPLACE "\\..*" "" baseline_int "${baseline}")

# Convert the fractional tolerance (0.30) to an integer percent (30).
set(tol_pct 30)
if(DEFINED TOLERANCE)
  string(REGEX REPLACE "^0\\." "" tol_frac "${TOLERANCE}")
  string(REGEX REPLACE "^([0-9][0-9]).*" "\\1" tol_pct "${tol_frac}")
endif()

math(EXPR floor_int "(${baseline_int} * (100 - ${tol_pct})) / 100")
message(STATUS
  "baseline=${baseline_int} current=${current_int} floor=${floor_int} jobs/sec (tolerance=${tol_pct}%)")

if(current_int LESS floor_int)
  message(FATAL_ERROR "throughput regression: ${current_int} < floor ${floor_int} jobs/sec")
endif()
message(STATUS "throughput within tolerance")
