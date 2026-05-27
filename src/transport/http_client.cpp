#include "transport/http_client.hpp"

#include <curl/curl.h>

#include <cstdio>

namespace clusterrun {

namespace {

std::size_t write_cb(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

}  // namespace

std::string url_encode(const std::string& s) {
  static const char hex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size() * 3);
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
        c == '_' || c == '.' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 0x0f]);
    }
  }
  return out;
}

HttpClient::HttpClient() {
  static const int once = []() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    return 0;
  }();
  (void)once;
  curl_ = curl_easy_init();
}

HttpClient::~HttpClient() {
  if (curl_ != nullptr) {
    curl_easy_cleanup(static_cast<CURL*>(curl_));
  }
}

HttpResponse HttpClient::perform(const std::string& method, const std::string& url,
                                 const std::string& body,
                                 const std::map<std::string, std::string>& headers) {
  HttpResponse resp;
  CURL* curl = static_cast<CURL*>(curl_);
  if (curl == nullptr) {
    return resp;
  }
  curl_easy_reset(curl);
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  struct curl_slist* hdr_list = nullptr;
  for (const auto& [k, v] : headers) {
    std::string line = k + ": " + v;
    hdr_list = curl_slist_append(hdr_list, line.c_str());
  }

  if (method == "PUT") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  } else if (method == "POST") {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  }

  if (hdr_list != nullptr) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr_list);
  }

  CURLcode rc = curl_easy_perform(curl);
  if (rc == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status);
  }
  if (hdr_list != nullptr) {
    curl_slist_free_all(hdr_list);
  }
  return resp;
}

HttpResponse HttpClient::get(const std::string& url) {
  return perform("GET", url, "", {});
}

HttpResponse HttpClient::put(const std::string& url, const std::string& body) {
  return perform("PUT", url, body, {{"Content-Type", "application/octet-stream"}});
}

HttpResponse HttpClient::post_form(const std::string& url, const std::string& form_body) {
  return perform("POST", url, form_body, {{"Content-Type", "application/x-www-form-urlencoded"}});
}

}  // namespace clusterrun
