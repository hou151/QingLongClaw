#include "QingLongClaw/config.h"

#include <fstream>
#include <iostream>
#include <tuple>
#include <unordered_map>

#include "QingLongClaw/util.h"

namespace QingLongClaw {

std::atomic_uint64_t Config::round_robin_counter_{0};

namespace {

json default_channels_json() {
  return json{
      {"telegram",
       {{"enabled", false}, {"token", ""}, {"proxy", ""}, {"allow_from", json::array()}}},
      {"discord", {{"enabled", false}, {"token", ""}, {"allow_from", json::array()}, {"mention_only", false}}},
      {"qq", {{"enabled", false}, {"app_id", ""}, {"app_secret", ""}, {"allow_from", json::array()}}},
      {"maixcam", {{"enabled", false}, {"host", "0.0.0.0"}, {"port", 18790}, {"allow_from", json::array()}}},
      {"whatsapp", {{"enabled", false}, {"bridge_url", "ws://localhost:3001"}, {"allow_from", json::array()}}},
      {"feishu",
       {{"enabled", false},
        {"mode", "long_connection"},
        {"api_base", "https://open.feishu.cn"},
        {"encrypt_key", ""},
        {"verification_token", ""},
        {"allow_from", json::array()}}},
      {"dingtalk",
       {{"enabled", false}, {"client_id", ""}, {"client_secret", ""}, {"allow_from", json::array()}}},
      {"slack", {{"enabled", false}, {"bot_token", ""}, {"app_token", ""}, {"allow_from", json::array()}}},
      {"line",
       {{"enabled", false},
        {"channel_secret", ""},
        {"channel_access_token", ""},
        {"webhook_host", "0.0.0.0"},
        {"webhook_port", 18791},
        {"webhook_path", "/webhook/line"},
        {"allow_from", json::array()}}},
      {"onebot",
       {{"enabled", false},
        {"ws_url", "ws://127.0.0.1:3001"},
        {"access_token", ""},
        {"reconnect_interval", 5},
        {"group_trigger_prefix", json::array()},
        {"allow_from", json::array()}}},
      {"wecom",
       {{"enabled", false},
        {"token", ""},
        {"encoding_aes_key", ""},
        {"webhook_url", ""},
        {"webhook_host", "0.0.0.0"},
        {"webhook_port", 18793},
        {"webhook_path", "/webhook/wecom"},
        {"allow_from", json::array()},
        {"reply_timeout", 5}}},
      {"wecom_app",
       {{"enabled", false},
        {"corp_id", ""},
        {"corp_secret", ""},
        {"agent_id", 0},
        {"token", ""},
        {"encoding_aes_key", ""},
        {"webhook_host", "0.0.0.0"},
        {"webhook_port", 18792},
        {"webhook_path", "/webhook/wecom-app"},
        {"allow_from", json::array()},
        {"reply_timeout", 5}}},
  };
}

json default_model_list_json() {
  return json::array({
      json{{"model_name", "glm-4.7"},
           {"model", "zhipu/glm-4.7"},
           {"api_base", "https://open.bigmodel.cn/api/paas/v4"},
           {"api_key", ""}},
      json{{"model_name", "deepseek-chat"},
           {"model", "deepseek/deepseek-chat"},
           {"api_base", "https://api.deepseek.com/v1"},
           {"api_key", ""}},
      json{{"model_name", "qwen-plus"},
           {"model", "qwen/qwen-plus"},
           {"api_base", "https://dashscope.aliyuncs.com/compatible-mode/v1"},
           {"api_key", ""}},
      json{{"model_name", "minimax-m2.1"},
           {"model", "minmax/MiniMax-M2.1"},
           {"api_base", "https://api.minimaxi.com/v1"},
           {"api_key", ""}},
  });
}

std::string get_string(const json& object, const std::string& key, const std::string& fallback = "") {
  if (!object.is_object() || !object.contains(key) || object[key].is_null()) {
    return fallback;
  }
  if (object[key].is_string()) {
    return object[key].get<std::string>();
  }
  return fallback;
}

bool get_bool(const json& object, const std::string& key, bool fallback = false) {
  if (!object.is_object() || !object.contains(key) || object[key].is_null()) {
    return fallback;
  }
  if (object[key].is_boolean()) {
    return object[key].get<bool>();
  }
  return fallback;
}

int get_int(const json& object, const std::string& key, int fallback = 0) {
  if (!object.is_object() || !object.contains(key) || object[key].is_null()) {
    return fallback;
  }
  if (object[key].is_number_integer()) {
    return object[key].get<int>();
  }
  return fallback;
}

std::optional<double> get_optional_double(const json& object, const std::string& key) {
  if (!object.is_object() || !object.contains(key) || object[key].is_null()) {
    return std::nullopt;
  }
  if (object[key].is_number()) {
    return object[key].get<double>();
  }
  return std::nullopt;
}

json build_provider_json(const std::string& api_key, const std::string& api_base, bool include_web_search = false) {
  json p{{"api_key", api_key}, {"api_base", api_base}};
  if (include_web_search) {
    p["web_search"] = true;
  }
  return p;
}

std::string provider_default_api_base(const std::string& provider) {
  static const std::unordered_map<std::string, std::string> bases = {
      {"zhipu", "https://open.bigmodel.cn/api/paas/v4"},
      {"deepseek", "https://api.deepseek.com/v1"},
      {"qwen", "https://dashscope.aliyuncs.com/compatible-mode/v1"},
      {"minmax", "https://api.minimaxi.com/v1"},
      {"minimax", "https://api.minimaxi.com/v1"},
  };
  const auto it = bases.find(provider);
  if (it == bases.end()) {
    return "";
  }
  return it->second;
}

}  // namespace

json Config::default_json() {
  return json{
      {"agents",
       {{"defaults",
         {{"workspace", "~/.qinglongclaw/workspace"},
          {"restrict_to_workspace", true},
          {"provider", ""},
          {"model_name", "glm-4.7"},
          {"model", "glm-4.7"},
           {"max_tokens", 4096},
           {"max_history_messages", 24},
           {"temperature", nullptr},
           {"max_tool_iterations", 0}}},
        {"list", json::array()}}},
      {"bindings", json::array()},
      {"session", {{"dm_scope", "main"}}},
      {"channels", default_channels_json()},
      {"providers",
       {{"zhipu", build_provider_json("", "https://open.bigmodel.cn/api/paas/v4")},
        {"deepseek", build_provider_json("", "https://api.deepseek.com/v1")},
        {"qwen", build_provider_json("", "https://dashscope.aliyuncs.com/compatible-mode/v1")},
        {"minmax", build_provider_json("", "https://api.minimaxi.com/v1")}}},
      {"model_list", default_model_list_json()},
      {"tools",
       {{"web",
         {{"brave", {{"enabled", false}, {"api_key", ""}, {"max_results", 5}}},
          {"tavily", {{"enabled", false}, {"api_key", ""}, {"max_results", 5}, {"base_url", ""}}},
          {"duckduckgo", {{"enabled", true}, {"max_results", 5}}},
          {"perplexity", {{"enabled", false}, {"api_key", ""}, {"max_results", 5}}},
          {"proxy", ""}}},
        {"cron", {{"exec_timeout_minutes", 5}}},
        {"exec", {{"enable_deny_patterns", true}, {"custom_deny_patterns", json::array()}}},
        {"skills",
         {{"registries",
           {{"clawhub",
             {{"enabled", true},
              {"base_url", "https://clawhub.ai"},
              {"search_path", "/api/v1/search"},
              {"skills_path", "/api/v1/skills"},
              {"download_path", "/api/v1/download"}}}}},
          {"max_concurrent_searches", 2},
          {"search_cache", {{"max_size", 50}, {"ttl_seconds", 300}}}}}}},
      {"heartbeat", {{"enabled", true}, {"interval", 30}}},
      {"devices", {{"enabled", false}, {"monitor_usb", true}}},
      {"gateway", {{"host", "0.0.0.0"}, {"port", 18790}}},
  };
}

ModelConfig Config::parse_model(const json& item) {
  ModelConfig model;
  if (!item.is_object()) {
    return model;
  }
  model.model_name = get_string(item, "model_name");
  model.model = get_string(item, "model");
  model.api_key = get_string(item, "api_key");
  model.api_base = get_string(item, "api_base");
  model.proxy = get_string(item, "proxy");
  model.auth_method = get_string(item, "auth_method");
  model.connect_mode = get_string(item, "connect_mode");
  model.workspace = get_string(item, "workspace");
  model.rpm = get_int(item, "rpm", 0);
  model.max_tokens_field = get_string(item, "max_tokens_field");
  return model;
}

bool Config::provider_section_has_auth(const json& provider) {
  if (!provider.is_object()) {
    return false;
  }
  const auto api_key = get_string(provider, "api_key");
  const auto api_base = get_string(provider, "api_base");
  const auto auth_method = get_string(provider, "auth_method");
  return !api_key.empty() || !api_base.empty() || !auth_method.empty();
}

Config Config::default_config() {
  Config cfg;
  cfg.raw = default_json();
  cfg.agents_defaults.workspace = "~/.qinglongclaw/workspace";
  cfg.agents_defaults.restrict_to_workspace = true;
  cfg.agents_defaults.provider = "";
  cfg.agents_defaults.model_name = "glm-4.7";
  cfg.agents_defaults.legacy_model = "glm-4.7";
  cfg.agents_defaults.max_tokens = 4096;
  cfg.agents_defaults.max_history_messages = 24;
  cfg.agents_defaults.temperature = std::nullopt;
  cfg.agents_defaults.max_tool_iterations = 0;

  for (const auto& item : cfg.raw["model_list"]) {
    cfg.model_list.push_back(parse_model(item));
  }
  return cfg;
}

Config Config::load(const std::filesystem::path& path) {
  Config cfg = Config::default_config();

  std::error_code ec;
  if (std::filesystem::exists(path, ec)) {
    std::ifstream input(path);
    if (input.good()) {
      try {
        json loaded = json::parse(input, nullptr, true, true);
        cfg.raw.merge_patch(loaded);
      } catch (const std::exception& ex) {
        std::cerr << "Warning: failed to parse config file '" << path.string()
                  << "', using defaults. Error: " << ex.what() << "\n";
      }
    }
  }

  if (cfg.raw.contains("providers") && cfg.raw["providers"].is_object()) {
    cfg.raw["providers"].erase("codex");
  }

  const json defaults = cfg.raw.value("agents", json::object()).value("defaults", json::object());
  cfg.agents_defaults.workspace = get_string(defaults, "workspace", cfg.agents_defaults.workspace);
  cfg.agents_defaults.restrict_to_workspace =
      get_bool(defaults, "restrict_to_workspace", cfg.agents_defaults.restrict_to_workspace);
  cfg.agents_defaults.provider = get_string(defaults, "provider", cfg.agents_defaults.provider);
  cfg.agents_defaults.model_name = get_string(defaults, "model_name", cfg.agents_defaults.model_name);
  cfg.agents_defaults.legacy_model = get_string(defaults, "model", cfg.agents_defaults.legacy_model);
  cfg.agents_defaults.max_tokens = get_int(defaults, "max_tokens", cfg.agents_defaults.max_tokens);
  cfg.agents_defaults.max_history_messages =
      get_int(defaults, "max_history_messages", cfg.agents_defaults.max_history_messages);
  cfg.agents_defaults.temperature = get_optional_double(defaults, "temperature");
  cfg.agents_defaults.max_tool_iterations =
      get_int(defaults, "max_tool_iterations", cfg.agents_defaults.max_tool_iterations);

  cfg.model_list.clear();
  if (cfg.raw.contains("model_list") && cfg.raw["model_list"].is_array() && !cfg.raw["model_list"].empty()) {
    for (const auto& item : cfg.raw["model_list"]) {
      ModelConfig model = parse_model(item);
      const auto lowered_model = to_lower(trim(model.model));
      if (lowered_model.rfind("codex/", 0) == 0) {
        continue;
      }
      if (!model.model_name.empty() && !model.model.empty()) {
        cfg.model_list.push_back(model);
      }
    }
  }

  if (cfg.model_list.empty() && cfg.raw.contains("providers") && cfg.raw["providers"].is_object()) {
    static const std::vector<std::tuple<std::string, std::string, std::string>> provider_defaults = {
        {"deepseek", "deepseek-chat", "deepseek-chat"},
        {"zhipu", "glm-4.7", "glm-4.7"},
        {"qwen", "qwen-plus", "qwen-plus"},
        {"minmax", "minimax-m2.1", "MiniMax-M2.1"},
    };
    const json providers = cfg.raw["providers"];
    for (const auto& [provider, default_model_name, default_model_id] : provider_defaults) {
      if (!providers.contains(provider)) {
        continue;
      }
      const auto section = providers[provider];
      if (!provider_section_has_auth(section)) {
        continue;
      }
      ModelConfig model;
      model.model_name = default_model_name;
      model.model = provider + "/" + default_model_id;
      model.api_key = get_string(section, "api_key");
      model.api_base = get_string(section, "api_base", provider_default_api_base(provider));
      model.proxy = get_string(section, "proxy");
      model.auth_method = get_string(section, "auth_method");
      cfg.model_list.push_back(model);
    }
  }

  return cfg;
}

bool Config::save(const std::filesystem::path& path) const {
  json output = raw;
  output["agents"]["defaults"]["workspace"] = agents_defaults.workspace;
  output["agents"]["defaults"]["restrict_to_workspace"] = agents_defaults.restrict_to_workspace;
  output["agents"]["defaults"]["provider"] = agents_defaults.provider;
  output["agents"]["defaults"]["model_name"] = agents_defaults.model_name;
  output["agents"]["defaults"]["model"] = agents_defaults.legacy_model;
  if (output["agents"]["defaults"].is_object()) {
    output["agents"]["defaults"].erase("codex_primary");
    output["agents"]["defaults"].erase("codex_fallback_on_error");
  }
  output["agents"]["defaults"]["max_tokens"] = agents_defaults.max_tokens;
  output["agents"]["defaults"]["max_history_messages"] = agents_defaults.max_history_messages;
  if (agents_defaults.temperature.has_value()) {
    output["agents"]["defaults"]["temperature"] = agents_defaults.temperature.value();
  } else {
    output["agents"]["defaults"]["temperature"] = nullptr;
  }
  output["agents"]["defaults"]["max_tool_iterations"] = agents_defaults.max_tool_iterations;

  output["model_list"] = json::array();
  for (const auto& model : model_list) {
    const auto lowered_model = to_lower(trim(model.model));
    if (lowered_model.rfind("codex/", 0) == 0) {
      continue;
    }
    json item{
        {"model_name", model.model_name},
        {"model", model.model},
        {"api_key", model.api_key},
    };
    if (!model.api_base.empty()) {
      item["api_base"] = model.api_base;
    }
    if (!model.proxy.empty()) {
      item["proxy"] = model.proxy;
    }
    if (!model.auth_method.empty()) {
      item["auth_method"] = model.auth_method;
    }
    if (!model.connect_mode.empty()) {
      item["connect_mode"] = model.connect_mode;
    }
    if (!model.workspace.empty()) {
      item["workspace"] = model.workspace;
    }
    if (model.rpm > 0) {
      item["rpm"] = model.rpm;
    }
    if (!model.max_tokens_field.empty()) {
      item["max_tokens_field"] = model.max_tokens_field;
    }
    output["model_list"].push_back(item);
  }

  if (output.contains("providers") && output["providers"].is_object()) {
    output["providers"].erase("codex");
  }
  if (output.contains("channels") && output["channels"].is_object() && output["channels"].contains("feishu") &&
      output["channels"]["feishu"].is_object()) {
    output["channels"]["feishu"]["app_id"] = "";
    output["channels"]["feishu"]["app_secret"] = "";
    output["channels"]["feishu"]["enabled"] = false;
  }

  return write_text_file(path, output.dump(2));
}

std::string Config::workspace_path() const { return expand_home(agents_defaults.workspace); }

std::string Config::default_model_name() const {
  if (!agents_defaults.model_name.empty()) {
    return agents_defaults.model_name;
  }
  return agents_defaults.legacy_model;
}

ResolvedModel Config::resolve_model(const std::optional<std::string>& override_model_name) const {
  const std::string requested = override_model_name.has_value() ? *override_model_name : default_model_name();

  std::vector<ModelConfig> matches;
  for (const auto& model : model_list) {
    if (model.model_name == requested) {
      matches.push_back(model);
    }
  }
  if (!matches.empty()) {
    const std::uint64_t idx = round_robin_counter_.fetch_add(1) % matches.size();
    const auto& selected = matches[idx];
    return ResolvedModel{
        selected.model_name,
        selected.model,
        selected.api_key,
        selected.api_base,
        selected.proxy,
        selected.auth_method,
        selected.max_tokens_field,
    };
  }

  std::string provider_name = to_lower(agents_defaults.provider);
  std::string provider_model = requested;
  if (provider_name.empty()) {
    const auto slash_pos = requested.find('/');
    if (slash_pos != std::string::npos) {
      provider_name = to_lower(requested.substr(0, slash_pos));
      provider_model = requested.substr(slash_pos + 1);
    }
  }

  const json providers = raw.value("providers", json::object());
  auto resolve_from_provider = [&](const std::string& provider_key,
                                   const std::string& fallback_model) -> std::optional<ResolvedModel> {
    if (!providers.contains(provider_key) || !providers[provider_key].is_object()) {
      return std::nullopt;
    }
    const auto section = providers[provider_key];
    const auto api_key = get_string(section, "api_key");
    const auto api_base = get_string(section, "api_base", provider_default_api_base(provider_key));
    const auto auth_method = get_string(section, "auth_method");
    if (api_key.empty() && auth_method.empty() && api_base.empty()) {
      return std::nullopt;
    }
    const std::string model_name = requested;
    const std::string model_value = provider_key + "/" + (provider_model.empty() ? fallback_model : provider_model);
    return ResolvedModel{
        model_name,
        model_value,
        api_key,
        api_base,
        get_string(section, "proxy"),
        auth_method,
        "",
    };
  };

  if (!provider_name.empty()) {
    if (auto resolved = resolve_from_provider(provider_name, requested); resolved.has_value()) {
      return *resolved;
    }
  }

  for (const auto& fallback_provider : {"zhipu", "deepseek", "qwen", "minmax"}) {
    if (auto resolved = resolve_from_provider(fallback_provider, requested); resolved.has_value()) {
      return *resolved;
    }
  }

  return ResolvedModel{requested, requested, "", "", "", "", ""};
}

std::filesystem::path default_config_path() { return config_root() / "config.json"; }

}  // namespace QingLongClaw
