#include "QingLongClaw/http_client.h"

#include <curl/curl.h>

#include <mutex>
#include <sstream>

namespace QingLongClaw {

namespace {

size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  const std::size_t total = size * nmemb;
  out->append(ptr, total);
  return total;
}

void ensure_curl_init() {
  static std::once_flag once;
  std::call_once(once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

}  // namespace

HttpResponse HttpClient::get(const std::string& url,
                             const std::map<std::string, std::string>& headers,
                             const std::string& proxy,
                             const long timeout_seconds) {
  return send("GET", url, "", headers, proxy, timeout_seconds);
}

HttpResponse HttpClient::post_json(const std::string& url,
                                   const std::string& json_body,
                                   const std::map<std::string, std::string>& headers,
                                   const std::string& proxy,
                                   const long timeout_seconds) {
  auto updated_headers = headers;
  if (updated_headers.find("Content-Type") == updated_headers.end()) {
    updated_headers["Content-Type"] = "application/json";
  }
  return send("POST", url, json_body, updated_headers, proxy, timeout_seconds);
}

HttpResponse HttpClient::send(const std::string& method,
                              const std::string& url,
                              const std::string& body,
                              const std::map<std::string, std::string>& headers,
                              const std::string& proxy,
                              const long timeout_seconds) {
  ensure_curl_init();

  HttpResponse response;
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    response.error = "failed to initialize curl";
    return response;
  }

  std::string response_body;
  struct curl_slist* header_list = nullptr;

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "QingLongClaw/0.1");

  if (!proxy.empty()) {
    curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());
  }

  if (method == "POST") {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  } else if (method != "GET") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    if (!body.empty()) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }
  }

  for (const auto& [key, value] : headers) {
    std::ostringstream line;
    line << key << ": " << value;
    header_list = curl_slist_append(header_list, line.str().c_str());
  }
  if (header_list != nullptr) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
  }

  const CURLcode code = curl_easy_perform(curl);
  if (code != CURLE_OK) {
    response.error = curl_easy_strerror(code);
  }

  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  response.status = status;
  response.body = std::move(response_body);

  if (header_list != nullptr) {
    curl_slist_free_all(header_list);
  }
  curl_easy_cleanup(curl);
  return response;
}

}  // namespace QingLongClaw
