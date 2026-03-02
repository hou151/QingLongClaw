#pragma once

#include <filesystem>
#include <regex>
#include <string>
#include <vector>

#include "QingLongClaw/provider.h"

namespace QingLongClaw {

struct ToolExecutionResult {
  bool ok = false;
  std::string output;
  std::string error;
};

class ToolExecutor {
 public:
  ToolExecutor(std::filesystem::path workspace, bool restrict_to_workspace);

  static std::vector<ToolDefinition> default_definitions();
  ToolExecutionResult execute(const ToolCall& tool_call) const;

 private:
  std::filesystem::path resolve_path(const std::string& path, bool allow_non_existing) const;
  bool is_dangerous_command(const std::string& command) const;
  ToolExecutionResult execute_shell(const std::string& command,
                                    const std::string& working_dir) const;
  ToolExecutionResult tool_read_file(const nlohmann::json& args) const;
  ToolExecutionResult tool_write_file(const nlohmann::json& args) const;
  ToolExecutionResult tool_append_file(const nlohmann::json& args) const;
  ToolExecutionResult tool_edit_file(const nlohmann::json& args) const;
  ToolExecutionResult tool_list_dir(const nlohmann::json& args) const;
  ToolExecutionResult tool_mkdir(const nlohmann::json& args) const;
  ToolExecutionResult tool_exec(const nlohmann::json& args) const;

  std::filesystem::path workspace_;
  bool restrict_to_workspace_ = true;
  std::vector<std::regex> command_deny_patterns_;
};

}  // namespace QingLongClaw
