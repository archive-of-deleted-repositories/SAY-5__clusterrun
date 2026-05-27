#include "worker/checkpoint.hpp"

#include <nlohmann/json.hpp>

#include "transport/localstack.hpp"

namespace clusterrun {

namespace {
using nlohmann::json;
}

std::string encode_checkpoint(const Checkpoint& c) {
  json j;
  j["step"] = c.step;
  j["state"] = c.state;
  j["ts_us"] = c.ts_us;
  return j.dump();
}

std::optional<Checkpoint> decode_checkpoint(const std::string& blob) {
  json j = json::parse(blob, nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded() || !j.is_object()) {
    return std::nullopt;
  }
  if (!j.contains("step") || !j.at("step").is_number_unsigned()) {
    return std::nullopt;
  }
  Checkpoint c;
  c.step = j.at("step").get<std::uint64_t>();
  if (j.contains("state")) {
    if (!j.at("state").is_string()) {
      return std::nullopt;
    }
    c.state = j.at("state").get<std::string>();
  }
  if (j.contains("ts_us")) {
    if (!j.at("ts_us").is_number_unsigned()) {
      return std::nullopt;
    }
    c.ts_us = j.at("ts_us").get<std::uint64_t>();
  }
  return c;
}

std::string checkpoint_key(std::uint64_t job_id) {
  return "checkpoints/" + std::to_string(job_id) + ".ckpt";
}

std::string write_checkpoint(ObjectStore& store, const std::string& bucket, std::uint64_t job_id,
                             const Checkpoint& c) {
  store.ensure_bucket(bucket);
  std::string key = checkpoint_key(job_id);
  if (!store.put(bucket, key, encode_checkpoint(c))) {
    return "";
  }
  return "s3://" + bucket + "/" + key;
}

std::optional<Checkpoint> read_checkpoint(ObjectStore& store, const std::string& s3_uri) {
  std::string bucket, key;
  if (!parse_s3_uri(s3_uri, &bucket, &key)) {
    return std::nullopt;
  }
  auto blob = store.get(bucket, key);
  if (!blob) {
    return std::nullopt;
  }
  return decode_checkpoint(*blob);
}

}  // namespace clusterrun
