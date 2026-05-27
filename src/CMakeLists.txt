add_library(clusterrun_core
  core/version.cpp
  core/types.cpp
  core/job_codec.cpp
  core/status_codec.cpp
)
target_link_libraries(clusterrun_core PUBLIC nlohmann_json::nlohmann_json)
target_include_directories(clusterrun_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

add_library(clusterrun_transport
  transport/http_client.cpp
  transport/localstack.cpp
)
target_link_libraries(clusterrun_transport PUBLIC clusterrun_core CURL::libcurl)
