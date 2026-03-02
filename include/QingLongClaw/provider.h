#pragma once

#include <optional>
#include <string>
#include <vector>

#include "json.hpp"
#include "QingLongClaw/config.h"

namespace QingLongClaw {

struct ToolCall {
  std::string id;
  std::string name;
  nlohmann::json arguments;
};

struct ChatMessage {
  std::string role;
  std::string content;
  std::string tool_call_id;
  std::vector<ToolCall> tool_calls;
};

struct ToolDefinition {
  std::string name;
  std::string description;
  nlohmann::json parameters;
};

struct ChatResult {
  std::string content;
  std::vector<ToolCall> tool_calls;
  std::string finish_reason;
  std::string error;
  nlohmann::json raw_response;

  bool ok() const { return error.empty(); }
};

class OpenAICompatProvider {
 public:
  explicit OpenAICompatProvider(ResolvedModel model);

  ChatResult chat(const std::vector<ChatMessage>& messages,
                  int max_tokens,
                  const std::optional<double>& temperature,
                  const std::vector<ToolDefinition>& tools = {}) const;

  const ResolvedModel& model() const { return model_; }

 private:
  static std::string normalize_model(const std::string& model, const std::string& api_base);
  std::string endpoint() const;

  ResolvedModel model_;
};

}  // namespace QingLongClaw
