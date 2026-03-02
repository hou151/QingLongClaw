#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "QingLongClaw/provider.h"

namespace QingLongClaw {

class SessionStore {
 public:
  explicit SessionStore(std::filesystem::path workspace);

  std::vector<ChatMessage> load(const std::string& session_key) const;
  bool save(const std::string& session_key, const std::vector<ChatMessage>& messages) const;
  void append(const std::string& session_key, const ChatMessage& message) const;

 private:
  std::filesystem::path session_path(const std::string& session_key) const;

  std::filesystem::path workspace_;
};

}  // namespace QingLongClaw

