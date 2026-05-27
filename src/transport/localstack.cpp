#include "transport/localstack.hpp"

#include <cstdlib>

namespace clusterrun {

namespace {

// Extracts the text between the first <tag> and </tag>, or empty if absent.
std::string xml_tag(const std::string& xml, const std::string& tag) {
  const std::string open = "<" + tag + ">";
  const std::string close = "</" + tag + ">";
  auto a = xml.find(open);
  if (a == std::string::npos) {
    return "";
  }
  a += open.size();
  auto b = xml.find(close, a);
  if (b == std::string::npos) {
    return "";
  }
  return xml.substr(a, b - a);
}

// Decodes the small set of XML entities LocalStack emits in message bodies.
std::string xml_unescape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) {
    if (s[i] == '&') {
      if (s.compare(i, 4, "&lt;") == 0) {
        out.push_back('<');
        i += 4;
      } else if (s.compare(i, 4, "&gt;") == 0) {
        out.push_back('>');
        i += 4;
      } else if (s.compare(i, 5, "&amp;") == 0) {
        out.push_back('&');
        i += 5;
      } else if (s.compare(i, 6, "&quot;") == 0) {
        out.push_back('"');
        i += 6;
      } else if (s.compare(i, 6, "&apos;") == 0) {
        out.push_back('\'');
        i += 6;
      } else {
        out.push_back(s[i++]);
      }
    } else {
      out.push_back(s[i++]);
    }
  }
  return out;
}

}  // namespace

std::string localstack_endpoint() {
  const char* env = std::getenv("AWS_ENDPOINT_URL");
  if (env != nullptr && env[0] != '\0') {
    return env;
  }
  return "http://localhost:4566";
}

bool parse_s3_uri(const std::string& uri, std::string* bucket, std::string* key) {
  const std::string scheme = "s3://";
  if (uri.compare(0, scheme.size(), scheme) != 0) {
    return false;
  }
  auto rest = uri.substr(scheme.size());
  auto slash = rest.find('/');
  if (slash == std::string::npos || slash == 0 || slash + 1 >= rest.size()) {
    return false;
  }
  *bucket = rest.substr(0, slash);
  *key = rest.substr(slash + 1);
  return true;
}

LocalStackQueue::LocalStackQueue(std::string endpoint) : endpoint_(std::move(endpoint)) {
}

std::string LocalStackQueue::ensure_queue(const std::string& name) {
  std::string form = "Action=CreateQueue&QueueName=" + url_encode(name) + "&Version=2012-11-05";
  HttpResponse r = http_.post_form(endpoint_, form);
  if (!r.ok()) {
    return "";
  }
  return xml_tag(r.body, "QueueUrl");
}

bool LocalStackQueue::send(const std::string& url, const std::string& body) {
  std::string form = "Action=SendMessage&QueueUrl=" + url_encode(url) +
                     "&MessageBody=" + url_encode(body) + "&Version=2012-11-05";
  HttpResponse r = http_.post_form(endpoint_, form);
  return r.ok();
}

std::vector<QueueMessage> LocalStackQueue::receive(const std::string& url, int max_messages,
                                                   int wait_seconds) {
  std::vector<QueueMessage> out;
  std::string form = "Action=ReceiveMessage&QueueUrl=" + url_encode(url) +
                     "&MaxNumberOfMessages=" + std::to_string(max_messages) +
                     "&WaitTimeSeconds=" + std::to_string(wait_seconds) + "&Version=2012-11-05";
  HttpResponse r = http_.post_form(endpoint_, form);
  if (!r.ok()) {
    return out;
  }
  // LocalStack returns one <Message> element per message. Walk them in order.
  std::size_t pos = 0;
  while (true) {
    auto a = r.body.find("<Message>", pos);
    if (a == std::string::npos) {
      break;
    }
    auto b = r.body.find("</Message>", a);
    if (b == std::string::npos) {
      break;
    }
    std::string chunk = r.body.substr(a, b - a);
    QueueMessage m;
    m.body = xml_unescape(xml_tag(chunk, "Body"));
    m.receipt = xml_tag(chunk, "ReceiptHandle");
    out.push_back(std::move(m));
    pos = b + 1;
  }
  return out;
}

bool LocalStackQueue::remove(const std::string& url, const std::string& receipt) {
  std::string form = "Action=DeleteMessage&QueueUrl=" + url_encode(url) +
                     "&ReceiptHandle=" + url_encode(receipt) + "&Version=2012-11-05";
  HttpResponse r = http_.post_form(endpoint_, form);
  return r.ok();
}

LocalStackObjectStore::LocalStackObjectStore(std::string endpoint)
    : endpoint_(std::move(endpoint)) {
}

bool LocalStackObjectStore::ensure_bucket(const std::string& bucket) {
  HttpResponse r = http_.put(endpoint_ + "/" + bucket, "");
  // 200 on create, 409 if it already exists: both are acceptable.
  return r.ok() || r.status == 409;
}

bool LocalStackObjectStore::put(const std::string& bucket, const std::string& key,
                                const std::string& body) {
  HttpResponse r = http_.put(endpoint_ + "/" + bucket + "/" + key, body);
  return r.ok();
}

std::optional<std::string> LocalStackObjectStore::get(const std::string& bucket,
                                                      const std::string& key) {
  HttpResponse r = http_.get(endpoint_ + "/" + bucket + "/" + key);
  if (!r.ok()) {
    return std::nullopt;
  }
  return r.body;
}

}  // namespace clusterrun
