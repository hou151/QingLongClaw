#pragma once

#include <string>

#include "json.hpp"

namespace QingLongClaw {

constexpr const char* kProjectName = "qinglongclaw";
constexpr const char* kProjectDisplayName = "QingLongClaw";
constexpr const char* kQingLongClawVersion = "0.1.2";
constexpr const char* kToolsApiVersion = "1.1";

std::string qinglongclaw_version();
nlohmann::json version_payload();
std::string version_text(bool detailed);

}  // namespace QingLongClaw
