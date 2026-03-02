#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "QingLongClaw/auth.h"
#include "QingLongClaw/config.h"
#include "QingLongClaw/provider.h"
#include "QingLongClaw/session_store.h"

namespace QingLongClaw {

struct AgentResponse {
  std::string content;
  std::string error;
  std::string usage_summary;

  bool ok() const { return error.empty(); }
};

class AgentRuntime {
 public:
  AgentRuntime(Config* config, AuthStore* auth_store);

  AgentResponse process_direct(const std::string& message,
                               const std::string& session_key,
                               const std::optional<std::string>& model_override = std::nullopt,
                               bool no_history = false,
                               const std::function<bool()>& should_cancel = {});

  std::string active_model_name() const;

 private:
  std::optional<std::string> handle_command(const std::string& input);
  std::string system_prompt() const;
  std::optional<ResolvedModel> resolve_model(const std::optional<std::string>& model_override) const;
  std::string provider_name_from_model(const std::string& model) const;
  std::vector<ChatMessage> build_messages(const std::vector<ChatMessage>& history,
                                          const std::string& user_input,
                                          std::size_t max_history_messages,
                                          const std::string& dynamic_context_hint) const;
  std::string tool_result_to_message(const ToolCall& call, bool ok, const std::string& text) const;

  Config* config_ = nullptr;
  AuthStore* auth_store_ = nullptr;
  std::string runtime_model_override_;
};

}  // namespace QingLongClaw
