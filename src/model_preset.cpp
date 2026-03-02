#include "QingLongClaw/model_preset.h"

#include <algorithm>
#include <unordered_set>

#include "json.hpp"
#include "QingLongClaw/util.h"

namespace QingLongClaw {

const std::vector<PresetModel>& supported_models() {
  static const std::vector<PresetModel> models = {
      {"glm-4.7", "zhipu/glm-4.7", "zhipu", "https://open.bigmodel.cn/api/paas/v4"},
      {"deepseek-chat", "deepseek/deepseek-chat", "deepseek", "https://api.deepseek.com/v1"},
      {"qwen-plus", "qwen/qwen-plus", "qwen", "https://dashscope.aliyuncs.com/compatible-mode/v1"},
      {"minimax-m2.1", "minmax/MiniMax-M2.1", "minmax", "https://api.minimaxi.com/v1"},
  };
  return models;
}

bool is_supported_model_name(const std::string& model_name) {
  for (const auto& preset : supported_models()) {
    if (preset.model_name == model_name) {
      return true;
    }
  }
  return false;
}

std::optional<ModelConfig> find_model_config(const Config& config, const std::string& model_name) {
  for (const auto& model : config.model_list) {
    if (model.model_name == model_name) {
      return model;
    }
  }
  return std::nullopt;
}

void apply_supported_model_preset(Config* config, const bool preserve_existing_api_keys) {
  if (config == nullptr) {
    return;
  }

  const auto original_models = config->model_list;

  std::vector<ModelConfig> rebuilt;
  rebuilt.reserve(supported_models().size() + original_models.size());

  std::unordered_set<std::string> preset_names;
  preset_names.reserve(supported_models().size());
  for (const auto& preset : supported_models()) {
    preset_names.insert(preset.model_name);
  }

  for (const auto& preset : supported_models()) {
    ModelConfig model;
    model.model_name = preset.model_name;
    model.model = preset.model;
    model.api_base = preset.api_base;
    model.api_key = "";
    model.proxy = "";

    if (preserve_existing_api_keys) {
      std::optional<ModelConfig> existing;
      for (const auto& candidate : original_models) {
        if (candidate.model_name == preset.model_name) {
          existing = candidate;
          break;
        }
      }
      if (!existing.has_value()) {
        for (const auto& candidate : original_models) {
          if (to_lower(candidate.model).rfind(preset.provider + "/", 0) == 0) {
            existing = candidate;
            break;
          }
        }
      }
      if (existing.has_value()) {
        if (!existing->api_key.empty()) {
          model.api_key = existing->api_key;
        }
        if (!existing->api_base.empty()) {
          model.api_base = existing->api_base;
        }
        model.proxy = existing->proxy;
        model.auth_method = existing->auth_method;
        model.max_tokens_field = existing->max_tokens_field;
      }
    }
    rebuilt.push_back(std::move(model));
  }

  for (const auto& model : original_models) {
    if (preset_names.find(model.model_name) != preset_names.end()) {
      continue;
    }
    if (trim(model.model_name).empty() || trim(model.model).empty()) {
      continue;
    }
    rebuilt.push_back(model);
  }

  config->model_list = rebuilt;

  nlohmann::json providers = config->raw.value("providers", nlohmann::json::object());
  if (!providers.is_object()) {
    providers = nlohmann::json::object();
  }
  for (const auto& preset : supported_models()) {
    auto current = find_model_config(*config, preset.model_name);
    providers[preset.provider] = {
        {"api_key", current.has_value() ? current->api_key : ""},
        {"api_base", current.has_value() && !current->api_base.empty() ? current->api_base : preset.api_base},
        {"proxy", current.has_value() ? current->proxy : ""},
    };
  }
  config->raw["providers"] = providers;

  const auto model_exists = [&](const std::string& model_name) {
    return std::any_of(config->model_list.begin(),
                       config->model_list.end(),
                       [&](const ModelConfig& model) { return model.model_name == model_name; });
  };

  if (!model_exists(config->agents_defaults.model_name)) {
    config->agents_defaults.model_name = supported_models().front().model_name;
  }
  if (!model_exists(config->agents_defaults.legacy_model)) {
    config->agents_defaults.legacy_model = config->agents_defaults.model_name;
  }
  if (!config->agents_defaults.provider.empty()) {
    const std::string lowered = to_lower(config->agents_defaults.provider);
    if (lowered != "zhipu" && lowered != "deepseek" && lowered != "qwen" && lowered != "minmax" &&
        lowered != "minimax") {
      config->agents_defaults.provider.clear();
    }
  }
}

void update_model_secret(Config* config,
                         const std::string& model_name,
                         const std::string& api_key,
                         const std::optional<std::string>& api_base,
                         const std::optional<std::string>& proxy) {
  if (config == nullptr) {
    return;
  }
  for (auto& model : config->model_list) {
    if (model.model_name != model_name) {
      continue;
    }
    model.api_key = api_key;
    if (api_base.has_value() && !api_base->empty()) {
      model.api_base = *api_base;
    }
    if (proxy.has_value()) {
      model.proxy = *proxy;
    }
    break;
  }
  apply_supported_model_preset(config, true);
}

}  // namespace QingLongClaw
