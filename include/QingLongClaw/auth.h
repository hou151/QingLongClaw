#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace QingLongClaw {

struct AuthCredential {
  std::string provider;
  std::string auth_method;
  std::string access_token;
  std::string refresh_token;
  std::string account_id;
  std::string email;
  std::string project_id;
  std::optional<std::int64_t> expires_at_ms;
  std::int64_t updated_at_ms = 0;

  bool is_expired() const;
  bool needs_refresh() const;
};

class AuthStore {
 public:
  explicit AuthStore(std::filesystem::path path);

  bool load();
  bool save() const;

  std::optional<AuthCredential> get(const std::string& provider) const;
  std::vector<AuthCredential> list() const;
  void upsert(const AuthCredential& credential);
  bool remove(const std::string& provider);
  void clear();

 private:
  std::filesystem::path path_;
  std::vector<AuthCredential> credentials_;
};

std::filesystem::path default_auth_store_path();

}  // namespace QingLongClaw
