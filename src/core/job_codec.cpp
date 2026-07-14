#include "core/job_codec.hpp"

#include <nlohmann/json.hpp>

namespace clusterrun {

namespace {

using nlohmann::json;

// Reads a vector<string> field, tolerating absence. Returns false if the field
// is present but is not an array of strings.
bool read_string_array(const json& obj, const char* key, std::vector<std::string>* out) {
  if (!obj.contains(key)) {
    return true;
  }
  const json& arr = obj.at(key);
  if (!arr.is_array()) {
    return false;
  }
  for (const auto& el : arr) {
    if (!el.is_string()) {
      return false;
    }
    out->push_back(el.get<std::string>());
  }
  return true;
}

}  // namespace

std::string encode_job(const Job& job) {
  json j;
  j["job_id"] = job.job_id;
  j["command"] = job.command;
  j["stdout_s3"] = job.stdout_s3;
  j["stderr_s3"] = job.stderr_s3;
  j["requires"] = {
      {"cpu_min", job.reqs.cpu_min},
      {"mem_gb_min", job.reqs.mem_gb_min},
      {"features", job.reqs.features},
      {"node_class", job.reqs.node_class},
  };
  j["attempts_max"] = job.attempts_max;
  j["checkpoint_interval_s"] = job.checkpoint_interval_s;
  j["idempotency_key"] = job.idempotency_key;
  j["priority"] = to_string(job.priority);
  j["tenant"] = job.tenant;
  if (!job.resume_from_checkpoint_s3.empty()) {
    j["resume_from_checkpoint_s3"] = job.resume_from_checkpoint_s3;
  }
  return j.dump();
}

std::optional<Job> decode_job(const std::string& text) {
  json j = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded() || !j.is_object()) {
    return std::nullopt;
  }

  Job job;
  // job_id is mandatory and must be an unsigned integer.
  if (!j.contains("job_id") || !j.at("job_id").is_number_unsigned()) {
    return std::nullopt;
  }
  job.job_id = j.at("job_id").get<JobId>();

  // command is mandatory and must be a non-empty array of strings.
  if (!read_string_array(j, "command", &job.command) || job.command.empty()) {
    return std::nullopt;
  }

  if (j.contains("stdout_s3")) {
    if (!j.at("stdout_s3").is_string()) {
      return std::nullopt;
    }
    job.stdout_s3 = j.at("stdout_s3").get<std::string>();
  }
  if (j.contains("stderr_s3")) {
    if (!j.at("stderr_s3").is_string()) {
      return std::nullopt;
    }
    job.stderr_s3 = j.at("stderr_s3").get<std::string>();
  }

  if (j.contains("requires")) {
    const json& r = j.at("requires");
    if (!r.is_object()) {
      return std::nullopt;
    }
    if (r.contains("cpu_min")) {
      if (!r.at("cpu_min").is_number_unsigned()) {
        return std::nullopt;
      }
      job.reqs.cpu_min = r.at("cpu_min").get<std::uint32_t>();
    }
    if (r.contains("mem_gb_min")) {
      if (!r.at("mem_gb_min").is_number_unsigned()) {
        return std::nullopt;
      }
      job.reqs.mem_gb_min = r.at("mem_gb_min").get<std::uint32_t>();
    }
    if (!read_string_array(r, "features", &job.reqs.features)) {
      return std::nullopt;
    }
    if (r.contains("node_class")) {
      if (!r.at("node_class").is_string()) {
        return std::nullopt;
      }
      job.reqs.node_class = r.at("node_class").get<std::string>();
    }
  }

  if (j.contains("attempts_max")) {
    if (!j.at("attempts_max").is_number_unsigned()) {
      return std::nullopt;
    }
    job.attempts_max = j.at("attempts_max").get<std::uint32_t>();
  }
  if (job.attempts_max == 0) {
    job.attempts_max = 1;
  }

  if (j.contains("checkpoint_interval_s")) {
    if (!j.at("checkpoint_interval_s").is_number_unsigned()) {
      return std::nullopt;
    }
    job.checkpoint_interval_s = j.at("checkpoint_interval_s").get<std::uint32_t>();
  }

  if (j.contains("idempotency_key")) {
    if (!j.at("idempotency_key").is_string()) {
      return std::nullopt;
    }
    job.idempotency_key = j.at("idempotency_key").get<std::string>();
  }
  if (j.contains("priority")) {
    if (!j.at("priority").is_string() ||
        !parse_priority(j.at("priority").get<std::string>(), &job.priority)) {
      return std::nullopt;
    }
  }
  if (j.contains("tenant")) {
    if (!j.at("tenant").is_string()) {
      return std::nullopt;
    }
    job.tenant = j.at("tenant").get<std::string>();
  }
  if (j.contains("resume_from_checkpoint_s3")) {
    if (!j.at("resume_from_checkpoint_s3").is_string()) {
      return std::nullopt;
    }
    job.resume_from_checkpoint_s3 = j.at("resume_from_checkpoint_s3").get<std::string>();
  }

  return job;
}

std::string encode_capability(const Capability& cap) {
  json j;
  j["worker_id"] = cap.worker_id;
  j["cpu"] = cap.cpu;
  j["mem_gb"] = cap.mem_gb;
  j["features"] = cap.features;
  j["node_class"] = cap.node_class;
  return j.dump();
}

std::optional<Capability> decode_capability(const std::string& text) {
  json j = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded() || !j.is_object()) {
    return std::nullopt;
  }
  Capability cap;
  if (!j.contains("worker_id") || !j.at("worker_id").is_number_unsigned()) {
    return std::nullopt;
  }
  cap.worker_id = j.at("worker_id").get<WorkerId>();
  if (j.contains("cpu")) {
    if (!j.at("cpu").is_number_unsigned()) {
      return std::nullopt;
    }
    cap.cpu = j.at("cpu").get<std::uint32_t>();
  }
  if (j.contains("mem_gb")) {
    if (!j.at("mem_gb").is_number_unsigned()) {
      return std::nullopt;
    }
    cap.mem_gb = j.at("mem_gb").get<std::uint32_t>();
  }
  if (!read_string_array(j, "features", &cap.features)) {
    return std::nullopt;
  }
  if (j.contains("node_class")) {
    if (!j.at("node_class").is_string()) {
      return std::nullopt;
    }
    cap.node_class = j.at("node_class").get<std::string>();
  }
  return cap;
}

}  // namespace clusterrun
