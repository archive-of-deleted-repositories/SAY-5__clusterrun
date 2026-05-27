add_library(clusterrun_core
  core/version.cpp
  core/types.cpp
  core/job_codec.cpp
  core/status_codec.cpp
)
target_link_libraries(clusterrun_core PUBLIC nlohmann_json::nlohmann_json CURL::libcurl)
target_include_directories(clusterrun_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
