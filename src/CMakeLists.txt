add_library(clusterrun_core
  core/version.cpp
  core/types.cpp
  core/job_codec.cpp
  core/status_codec.cpp
  core/base64.cpp
)
target_link_libraries(clusterrun_core PUBLIC nlohmann_json::nlohmann_json)
target_include_directories(clusterrun_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

add_library(clusterrun_transport
  transport/http_client.cpp
  transport/localstack.cpp
)
target_link_libraries(clusterrun_transport PUBLIC clusterrun_core CURL::libcurl)

add_library(clusterrun_controller_lib
  controller/match.cpp
  controller/scheduler.cpp
  controller/heartbeat.cpp
  controller/controller.cpp
  controller/runtime.cpp
)
target_link_libraries(clusterrun_controller_lib PUBLIC clusterrun_core clusterrun_transport)

add_library(clusterrun_worker_lib
  worker/exec.cpp
  worker/worker.cpp
  worker/runtime.cpp
)
target_link_libraries(clusterrun_worker_lib PUBLIC clusterrun_core clusterrun_transport)

add_subdirectory(controller)
add_subdirectory(worker)
