#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "json.hpp"

namespace QingLongClaw {

using json = nlohmann::json;

struct AgentDefaults {
  std::string workspace = "~/.qinglongclaw/workspace";
  bool restrict_to_workspace = true;
  std::string provider;
  std::string model_name = "glm-4.7";
  std::string legacy_model = "glm-4.7";
  int max_tokens = 8192;
  int max_history_messages = 24;
  std::optional<double> temperature;
  int max_tool_iterations = 0;  // 0 means unlimited
};

struct ModelConfig {
  std::string model_name;
  std::string model;
  std::string api_key;
  std::string api_base;
  std::string proxy;
  std::string auth_method;
  std::string connect_mode;
  std::string workspace;
  int rpm = 0;
  std::string max_tokens_field;
};

struct ResolvedModel {
  std::string model_name;
  std::string model;
  std::string api_key;
  std::string api_base;
  std::string proxy;
  std::string auth_method;
  std::string max_tokens_field;
};

class Config {
 public:
  static Config default_config();
  static Config load(const std::filesystem::path& path);

  bool save(const std::filesystem::path& path) const;

  std::string workspace_path() const;
  std::string default_model_name() const;
  ResolvedModel resolve_model(const std::optional<std::string>& override_model_name = std::nullopt) const;

  AgentDefaults agents_defaults;
  std::vector<ModelConfig> model_list;
  json raw;

 private:
  static json default_json();
  static ModelConfig parse_model(const json& item);
  static bool provider_section_has_auth(const json& provider);

  static std::atomic_uint64_t round_robin_counter_;
};

std::filesystem::path default_config_path();

}  // namespace QingLongClaw
