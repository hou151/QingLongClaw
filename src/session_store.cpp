#include "QingLongClaw/session_store.h"

#include "json.hpp"
#include "QingLongClaw/util.h"

namespace QingLongClaw {

namespace {

std::string dump_json_lossy(const nlohmann::json& value, const int indent = -1) {
  return value.dump(indent, ' ', false, nlohmann::json::error_handler_t::replace);
}

bool parse_json_lossy(const std::string& value, nlohmann::json* out) {
  if (out == nullptr) {
    return false;
  }
  auto parsed = nlohmann::json::parse(value, nullptr, false);
  if (!parsed.is_discarded()) {
    *out = std::move(parsed);
    return true;
  }
  parsed = nlohmann::json::parse(sanitize_utf8_lossy(value), nullptr, false);
  if (parsed.is_discarded()) {
    return false;
  }
  *out = std::move(parsed);
  return true;
}

}  // namespace

SessionStore::SessionStore(std::filesystem::path workspace) : workspace_(std::move(workspace)) {}

std::filesystem::path SessionStore::session_path(const std::string& session_key) const {
  const std::string filename = sanitize_filename(session_key) + ".json";
  return workspace_ / "sessions" / filename;
}

std::vector<ChatMessage> SessionStore::load(const std::string& session_key) const {
  std::vector<ChatMessage> messages;
  const auto path = session_path(session_key);
  const std::string data = read_text_file(path);
  if (data.empty()) {
    return messages;
  }

  nlohmann::json root;
  if (!parse_json_lossy(data, &root) || !root.is_object() || !root.contains("messages") || !root["messages"].is_array()) {
    return messages;
  }
  for (const auto& item : root["messages"]) {
    if (!item.is_object()) {
      continue;
    }
    ChatMessage message;
    message.role = sanitize_utf8_lossy(item.value("role", ""));
    message.content = sanitize_utf8_lossy(item.value("content", ""));
    if (!message.role.empty()) {
      messages.push_back(std::move(message));
    }
  }
  return messages;
}

bool SessionStore::save(const std::string& session_key, const std::vector<ChatMessage>& messages) const {
  try {
    nlohmann::json root;
    root["session_key"] = sanitize_utf8_lossy(session_key);
    root["updated_at_ms"] = unix_ms_now();
    root["messages"] = nlohmann::json::array();
    for (const auto& message : messages) {
      root["messages"].push_back(
          nlohmann::json{{"role", sanitize_utf8_lossy(message.role)}, {"content", sanitize_utf8_lossy(message.content)}});
    }
    return write_text_file(session_path(session_key), dump_json_lossy(root, 2));
  } catch (...) {
    return false;
  }
}

void SessionStore::append(const std::string& session_key, const ChatMessage& message) const {
  auto messages = load(session_key);
  messages.push_back(message);
  save(session_key, messages);
}

}  // namespace QingLongClaw
