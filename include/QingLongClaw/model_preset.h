#pragma once

#include <optional>
#include <string>
#include <vector>

#include "QingLongClaw/config.h"

namespace QingLongClaw {

struct PresetModel {
  std::string model_name;
  std::string model;
  std::string provider;
  std::string api_base;
};

const std::vector<PresetModel>& supported_models();
bool is_supported_model_name(const std::string& model_name);
void apply_supported_model_preset(Config* config, bool preserve_existing_api_keys = true);
void update_model_secret(Config* config,
                         const std::string& model_name,
                         const std::string& api_key,
                         const std::optional<std::string>& api_base = std::nullopt,
                         const std::optional<std::string>& proxy = std::nullopt);
std::optional<ModelConfig> find_model_config(const Config& config, const std::string& model_name);

}  // namespace QingLongClaw
