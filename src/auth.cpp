#include "QingLongClaw/auth.h"

#include <algorithm>

#include "json.hpp"
#include "QingLongClaw/util.h"

namespace QingLongClaw {

bool AuthCredential::is_expired() const {
  if (!expires_at_ms.has_value()) {
    return false;
  }
  return unix_ms_now() >= expires_at_ms.value();
}

bool AuthCredential::needs_refresh() const {
  if (!expires_at_ms.has_value()) {
    return false;
  }
  constexpr std::int64_t refresh_window_ms = 5 * 60 * 1000;
  return unix_ms_now() + refresh_window_ms >= expires_at_ms.value();
}

AuthStore::AuthStore(std::filesystem::path path) : path_(std::move(path)) {}

bool AuthStore::load() {
  credentials_.clear();
  const std::string data = read_text_file(path_);
  if (data.empty()) {
    return true;
  }

  try {
    const auto root = nlohmann::json::parse(data);
    if (!root.is_object() || !root.contains("credentials") || !root["credentials"].is_array()) {
      return false;
    }
    for (const auto& item : root["credentials"]) {
      if (!item.is_object()) {
        continue;
      }
      AuthCredential cred;
      cred.provider = item.value("provider", "");
      cred.auth_method = item.value("auth_method", "");
      cred.access_token = item.value("access_token", "");
      cred.refresh_token = item.value("refresh_token", "");
      cred.account_id = item.value("account_id", "");
      cred.email = item.value("email", "");
      cred.project_id = item.value("project_id", "");
      cred.updated_at_ms = item.value("updated_at_ms", 0LL);
      if (item.contains("expires_at_ms") && item["expires_at_ms"].is_number_integer()) {
        cred.expires_at_ms = item["expires_at_ms"].get<std::int64_t>();
      }
      if (!cred.provider.empty()) {
        credentials_.push_back(std::move(cred));
      }
    }
    return true;
  } catch (...) {
    return false;
  }
}

bool AuthStore::save() const {
  nlohmann::json root;
  root["version"] = 1;
  root["credentials"] = nlohmann::json::array();
  for (const auto& cred : credentials_) {
    nlohmann::json item{
        {"provider", cred.provider},
        {"auth_method", cred.auth_method},
        {"access_token", cred.access_token},
        {"refresh_token", cred.refresh_token},
        {"account_id", cred.account_id},
        {"email", cred.email},
        {"project_id", cred.project_id},
        {"updated_at_ms", cred.updated_at_ms},
    };
    if (cred.expires_at_ms.has_value()) {
      item["expires_at_ms"] = cred.expires_at_ms.value();
    }
    root["credentials"].push_back(item);
  }
  return write_text_file(path_, root.dump(2));
}

std::optional<AuthCredential> AuthStore::get(const std::string& provider) const {
  for (const auto& credential : credentials_) {
    if (credential.provider == provider) {
      return credential;
    }
  }
  return std::nullopt;
}

std::vector<AuthCredential> AuthStore::list() const { return credentials_; }

void AuthStore::upsert(const AuthCredential& credential) {
  for (auto& existing : credentials_) {
    if (existing.provider == credential.provider) {
      existing = credential;
      return;
    }
  }
  credentials_.push_back(credential);
}

bool AuthStore::remove(const std::string& provider) {
  const auto old_size = credentials_.size();
  credentials_.erase(std::remove_if(credentials_.begin(), credentials_.end(),
                                    [&](const AuthCredential& credential) {
                                      return credential.provider == provider;
                                    }),
                     credentials_.end());
  return credentials_.size() != old_size;
}

void AuthStore::clear() { credentials_.clear(); }

std::filesystem::path default_auth_store_path() { return config_root() / "auth.json"; }

}  // namespace QingLongClaw

