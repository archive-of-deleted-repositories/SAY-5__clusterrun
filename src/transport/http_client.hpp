#pragma once

#include <map>
#include <string>

namespace clusterrun {

struct HttpResponse {
  long status = 0;
  std::string body;
  bool ok() const {
    return status >= 200 && status < 300;
  }
};

// A thin synchronous HTTP client over libcurl, scoped to what the LocalStack
// SQS and S3 REST endpoints need. Not a general purpose client.
class HttpClient {
 public:
  HttpClient();
  ~HttpClient();

  HttpClient(const HttpClient&) = delete;
  HttpClient& operator=(const HttpClient&) = delete;

  HttpResponse get(const std::string& url);
  HttpResponse put(const std::string& url, const std::string& body);
  HttpResponse post_form(const std::string& url, const std::string& form_body);

 private:
  HttpResponse perform(const std::string& method, const std::string& url, const std::string& body,
                       const std::map<std::string, std::string>& headers);
  void* curl_;  // CURL*
};

// Percent-encodes a string for use in an application/x-www-form-urlencoded body.
std::string url_encode(const std::string& s);

}  // namespace clusterrun
