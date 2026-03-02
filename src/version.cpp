#include "QingLongClaw/version.h"

#include <curl/curl.h>

#include <sstream>
#include <string>
#include <vector>

#include "httplib.h"
#include "QingLongClaw/model_preset.h"
#include "QingLongClaw/tools.h"

namespace QingLongClaw {

namespace {

std::string compiler_version() {
#if defined(__clang__)
  std::ostringstream out;
  out << "clang " << __clang_major__ << "." << __clang_minor__ << "." << __clang_patchlevel__;
  return out.str();
#elif defined(__GNUC__)
  std::ostringstream out;
  out << "gcc " << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__;
  return out.str();
#elif defined(_MSC_VER)
  std::ostringstream out;
  out << "msvc " << _MSC_VER;
  return out.str();
#else
  return "unknown";
#endif
}

std::string build_architecture() {
#if defined(__aarch64__) || defined(_M_ARM64)
  return "aarch64";
#elif defined(__x86_64__) || defined(_M_X64)
  return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
  return "x86";
#elif defined(__arm__) || defined(_M_ARM)
  return "arm";
#else
  return "unknown";
#endif
}

std::string libcurl_runtime_version() {
  const curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
  if (info == nullptr || info->version == nullptr) {
    return "unknown";
  }
  return info->version;
}

std::string json_version() {
  std::ostringstream out;
  out << NLOHMANN_JSON_VERSION_MAJOR << "." << NLOHMANN_JSON_VERSION_MINOR << "."
      << NLOHMANN_JSON_VERSION_PATCH;
  return out.str();
}

}  // namespace

std::string qinglongclaw_version() { return kQingLongClawVersion; }

nlohmann::json version_payload() {
  nlohmann::json payload;
  payload["project"] = kProjectName;
  payload["project_display_name"] = kProjectDisplayName;
  payload["qinglongclaw"] = qinglongclaw_version();
  payload["build"] = {
      {"compiler", compiler_version()},
      {"cxx_standard", static_cast<long long>(__cplusplus)},
      {"arch", build_architecture()},
      {"date", __DATE__},
      {"time", __TIME__},
  };
  payload["dependencies"] = {
      {"libcurl_compile", LIBCURL_VERSION},
      {"libcurl_runtime", libcurl_runtime_version()},
      {"cpp_httplib", CPPHTTPLIB_VERSION},
      {"nlohmann_json", json_version()},
      {"croncpp", "header-only (cpp17)"},
  };

  payload["tools"] = {
      {"api_version", kToolsApiVersion},
      {"functions", nlohmann::json::array()},
  };
  for (const auto& tool : ToolExecutor::default_definitions()) {
    payload["tools"]["functions"].push_back(tool.name);
  }

  payload["models"] = nlohmann::json::array();
  for (const auto& model : supported_models()) {
    payload["models"].push_back({
        {"model_name", model.model_name},
        {"model", model.model},
        {"provider", model.provider},
    });
  }
  return payload;
}

std::string version_text(const bool detailed) {
  if (!detailed) {
    return std::string(kProjectName) + " " + qinglongclaw_version();
  }

  const auto payload = version_payload();
  std::ostringstream out;
  out << kProjectName << " " << qinglongclaw_version() << "\n";
  out << "compiler: " << payload["build"].value("compiler", "unknown") << "\n";
  out << "arch: " << payload["build"].value("arch", "unknown") << "\n";
  out << "cxx_standard: " << payload["build"].value("cxx_standard", static_cast<long long>(0)) << "\n";
  out << "libcurl: compile " << payload["dependencies"].value("libcurl_compile", "unknown") << ", runtime "
      << payload["dependencies"].value("libcurl_runtime", "unknown") << "\n";
  out << "cpp-httplib: " << payload["dependencies"].value("cpp_httplib", "unknown") << "\n";
  out << "nlohmann/json: " << payload["dependencies"].value("nlohmann_json", "unknown") << "\n";
  out << "tools(api " << payload["tools"].value("api_version", "unknown") << "): ";
  const auto tools = payload["tools"].value("functions", nlohmann::json::array());
  for (std::size_t i = 0; i < tools.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    out << tools[i].get<std::string>();
  }
  out << "\nmodels: ";
  const auto models = payload.value("models", nlohmann::json::array());
  for (std::size_t i = 0; i < models.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    out << models[i].value("model_name", "");
  }
  return out.str();
}

}  // namespace QingLongClaw
