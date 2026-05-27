# libFuzzer targets. Enabled with -DCLUSTERRUN_BUILD_FUZZ=ON and require a Clang
# that ships libFuzzer (-fsanitize=fuzzer).
add_executable(fuzz_job_codec fuzz_job_codec.cpp)
target_link_libraries(fuzz_job_codec PRIVATE clusterrun_core)
target_compile_options(fuzz_job_codec PRIVATE -g -fsanitize=fuzzer,address,undefined)
target_link_options(fuzz_job_codec PRIVATE -fsanitize=fuzzer,address,undefined)
