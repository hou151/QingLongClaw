#pragma once

#include <map>
#include <string>

namespace QingLongClaw {

struct HttpResponse {
  long status = 0;
  std::string body;
  std::string error;

  bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

class HttpClient {
 public:
  static HttpResponse get(const std::string& url,
                          const std::map<std::string, std::string>& headers = {},
                          const std::string& proxy = "",
                          long timeout_seconds = 30);
  static HttpResponse post_json(const std::string& url,
                                const std::string& json_body,
                                const std::map<std::string, std::string>& headers = {},
                                const std::string& proxy = "",
                                long timeout_seconds = 120);

 private:
  static HttpResponse send(const std::string& method,
                           const std::string& url,
                           const std::string& body,
                           const std::map<std::string, std::string>& headers,
                           const std::string& proxy,
                           long timeout_seconds);
};

}  // namespace QingLongClaw

