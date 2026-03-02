#include "QingLongClaw/tools.h"

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <stdexcept>

#include "QingLongClaw/util.h"

namespace QingLongClaw {

namespace {

std::string tool_error_text(const std::string& name, const std::string& error) {
  return "[tool:" + name + " error] " + error;
}

std::string truncate_text(const std::string& text, const std::size_t max_len) {
  if (text.size() <= max_len) {
    return text;
  }
  return text.substr(0, max_len) + "\n... (truncated)";
}

std::string join_dir_output(const std::vector<std::filesystem::directory_entry>& entries) {
  std::ostringstream out;
  for (const auto& entry : entries) {
    out << (entry.is_directory() ? "DIR:  " : "FILE: ") << entry.path().filename().string() << "\n";
  }
  return out.str();
}

std::size_t count_occurrences(const std::string& text, const std::string& needle) {
  if (needle.empty()) {
    return 0;
  }
  std::size_t count = 0;
  std::size_t pos = 0;
  while ((pos = text.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

}  // namespace

ToolExecutor::ToolExecutor(std::filesystem::path workspace, const bool restrict_to_workspace)
    : workspace_(std::move(workspace)), restrict_to_workspace_(restrict_to_workspace) {
  command_deny_patterns_.emplace_back(std::regex(R"(\brm\s+-[rf]{1,2}\b)", std::regex::icase));
  command_deny_patterns_.emplace_back(std::regex(R"(\bmkfs\b)", std::regex::icase));
  command_deny_patterns_.emplace_back(std::regex(R"(\bdd\s+if=)", std::regex::icase));
  command_deny_patterns_.emplace_back(std::regex(R"(\bshutdown\b|\breboot\b|\bpoweroff\b)", std::regex::icase));
  command_deny_patterns_.emplace_back(std::regex(R"(\bsudo\b)", std::regex::icase));
  command_deny_patterns_.emplace_back(std::regex(R"(\|\s*(sh|bash)\b)", std::regex::icase));
  command_deny_patterns_.emplace_back(std::regex(R"(\$\([^)]+\))", std::regex::icase));
  command_deny_patterns_.emplace_back(std::regex(R"(`[^`]+`)", std::regex::icase));
}

std::vector<ToolDefinition> ToolExecutor::default_definitions() {
  return {
      ToolDefinition{
          "list_dir",
          "List files and directories in a path. Use this before reading or writing files.",
          nlohmann::json{
              {"type", "object"},
              {"properties",
               {{"path", {{"type", "string"}, {"description", "Directory path, default is '.'"}}}}},
          },
      },
      ToolDefinition{
          "read_file",
          "Read text content from a local file.",
          nlohmann::json{
              {"type", "object"},
              {"properties",
               {{"path", {{"type", "string"}, {"description", "File path to read"}}},
                {"offset", {{"type", "integer"}, {"description", "Optional start offset in characters"}}},
                {"max_chars",
                 {{"type", "integer"},
                  {"description", "Optional max characters to read (default 200000, max 400000)"}}}}},
              {"required", nlohmann::json::array({"path"})},
          },
      },
      ToolDefinition{
          "write_file",
          "Write full text content to a local file (overwrite).",
          nlohmann::json{
              {"type", "object"},
              {"properties",
               {{"path", {{"type", "string"}, {"description", "File path to write"}}},
                {"content", {{"type", "string"}, {"description", "File content"}}}}},
              {"required", nlohmann::json::array({"path", "content"})},
          },
      },
      ToolDefinition{
          "append_file",
          "Append text content to the end of a local file.",
          nlohmann::json{
              {"type", "object"},
              {"properties",
               {{"path", {{"type", "string"}, {"description", "File path to append"}}},
                {"content", {{"type", "string"}, {"description", "Content to append"}}}}},
              {"required", nlohmann::json::array({"path", "content"})},
          },
      },
      ToolDefinition{
          "edit_file",
          "Replace exact text in a file. old_text must match exactly and appear once.",
          nlohmann::json{
              {"type", "object"},
              {"properties",
               {{"path", {{"type", "string"}, {"description", "File path to edit"}}},
                {"old_text", {{"type", "string"}, {"description", "Exact text to replace"}}},
                {"new_text", {{"type", "string"}, {"description", "Replacement text"}}}}},
              {"required", nlohmann::json::array({"path", "old_text", "new_text"})},
          },
      },
      ToolDefinition{
          "mkdir",
          "Create a directory recursively.",
          nlohmann::json{
              {"type", "object"},
              {"properties",
               {{"path", {{"type", "string"}, {"description", "Directory path to create"}}}}},
              {"required", nlohmann::json::array({"path"})},
          },
      },
      ToolDefinition{
          "exec",
          "Execute a shell command in local workspace and return output.",
          nlohmann::json{
              {"type", "object"},
              {"properties",
               {{"command", {{"type", "string"}, {"description", "Shell command"}}},
                {"working_dir", {{"type", "string"}, {"description", "Working directory (optional)"}}}}},
              {"required", nlohmann::json::array({"command"})},
          },
      },
  };
}

std::filesystem::path ToolExecutor::resolve_path(const std::string& path, const bool allow_non_existing) const {
  std::filesystem::path candidate(path.empty() ? "." : path);
  if (!candidate.is_absolute()) {
    candidate = workspace_ / candidate;
  }
  candidate = candidate.lexically_normal();

  if (!restrict_to_workspace_) {
    return candidate;
  }

  std::error_code ec;
  auto workspace_abs = std::filesystem::weakly_canonical(workspace_, ec);
  if (ec) {
    ec.clear();
    workspace_abs = std::filesystem::absolute(workspace_, ec);
    if (ec) {
      throw std::runtime_error("workspace path is invalid");
    }
  }

  std::filesystem::path target_for_check = candidate;
  if (allow_non_existing && !std::filesystem::exists(candidate, ec)) {
    target_for_check = candidate.parent_path();
  }
  target_for_check = std::filesystem::weakly_canonical(target_for_check, ec);
  if (ec) {
    ec.clear();
    target_for_check = std::filesystem::absolute(target_for_check, ec);
    if (ec) {
      throw std::runtime_error("invalid target path");
    }
  }

  const auto rel = std::filesystem::relative(target_for_check, workspace_abs, ec);
  if (ec || rel.empty() || rel.string().rfind("..", 0) == 0) {
    throw std::runtime_error("access denied: path is outside workspace");
  }

  return candidate;
}

bool ToolExecutor::is_dangerous_command(const std::string& command) const {
  const std::string lowered = to_lower(command);
  for (const auto& pattern : command_deny_patterns_) {
    if (std::regex_search(lowered, pattern)) {
      return true;
    }
  }
  return false;
}

ToolExecutionResult ToolExecutor::execute_shell(const std::string& command,
                                                const std::string& working_dir) const {
  if (command.empty()) {
    return {false, "", "command is empty"};
  }
  if (is_dangerous_command(command)) {
    return {false, "", "command blocked by safety guard"};
  }

  std::filesystem::path wd = workspace_;
  try {
    wd = resolve_path(working_dir.empty() ? "." : working_dir, false);
  } catch (const std::exception& ex) {
    return {false, "", ex.what()};
  }

  const std::string escaped_wd = replace_all(wd.string(), "\"", "\\\"");
  std::string shell_command;
#ifdef _WIN32
  shell_command = "cd /d \"" + escaped_wd + "\" && " + command + " 2>&1";
  FILE* pipe = _popen(shell_command.c_str(), "r");
#else
  shell_command = "cd \"" + escaped_wd + "\" && " + command + " 2>&1";
  FILE* pipe = popen(shell_command.c_str(), "r");
#endif
  if (pipe == nullptr) {
    return {false, "", "failed to start command"};
  }

  std::string output;
  char buffer[4096];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output += buffer;
    if (output.size() > 20000) {
      output = truncate_text(output, 20000);
      break;
    }
  }

#ifdef _WIN32
  const int status = _pclose(pipe);
#else
  const int status = pclose(pipe);
#endif

  if (output.empty()) {
    output = "(no output)";
  }
  if (status != 0) {
    return {false, output, "command exit code is non-zero"};
  }
  return {true, output, ""};
}

ToolExecutionResult ToolExecutor::tool_read_file(const nlohmann::json& args) const {
  if (!args.contains("path") || !args["path"].is_string()) {
    return {false, "", "path is required"};
  }

  std::size_t offset = 0;
  if (args.contains("offset")) {
    if (!args["offset"].is_number_integer() || args["offset"].get<long long>() < 0) {
      return {false, "", "offset must be a non-negative integer"};
    }
    offset = static_cast<std::size_t>(args["offset"].get<long long>());
  }

  std::size_t max_chars = 200000;
  if (args.contains("max_chars")) {
    if (!args["max_chars"].is_number_integer() || args["max_chars"].get<long long>() <= 0) {
      return {false, "", "max_chars must be a positive integer"};
    }
    max_chars = static_cast<std::size_t>(args["max_chars"].get<long long>());
    max_chars = std::min<std::size_t>(max_chars, 400000);
  }

  try {
    const auto path = resolve_path(args["path"].get<std::string>(), true);
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
      return {false, "", "path is a directory"};
    }
    const auto content = read_text_file(path);
    if (content.empty() && !std::filesystem::exists(path)) {
      return {false, "", "file not found"};
    }

    if (offset >= content.size()) {
      return {true, "", ""};
    }

    const std::size_t read_len = std::min(max_chars, content.size() - offset);
    std::string chunk = content.substr(offset, read_len);
    if (offset + read_len < content.size()) {
      chunk += "\n... (truncated, read " + std::to_string(read_len) + " chars at offset " +
               std::to_string(offset) + " / total " + std::to_string(content.size()) + ")";
    }
    return {true, chunk, ""};
  } catch (const std::exception& ex) {
    return {false, "", ex.what()};
  }
}

ToolExecutionResult ToolExecutor::tool_write_file(const nlohmann::json& args) const {
  if (!args.contains("path") || !args["path"].is_string()) {
    return {false, "", "path is required"};
  }
  if (!args.contains("content") || !args["content"].is_string()) {
    return {false, "", "content is required"};
  }
  try {
    const auto path = resolve_path(args["path"].get<std::string>(), true);
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
      return {false, "", "path is a directory"};
    }
    if (!write_text_file(path, args["content"].get<std::string>())) {
      return {false, "", "failed to write file"};
    }
    return {true, "File written: " + path.string(), ""};
  } catch (const std::exception& ex) {
    return {false, "", ex.what()};
  }
}

ToolExecutionResult ToolExecutor::tool_append_file(const nlohmann::json& args) const {
  if (!args.contains("path") || !args["path"].is_string()) {
    return {false, "", "path is required"};
  }
  if (!args.contains("content") || !args["content"].is_string()) {
    return {false, "", "content is required"};
  }
  try {
    const auto path = resolve_path(args["path"].get<std::string>(), true);
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
      return {false, "", "path is a directory"};
    }
    std::string merged = read_text_file(path);
    merged += args["content"].get<std::string>();
    if (!write_text_file(path, merged)) {
      return {false, "", "failed to append file"};
    }
    return {true, "Appended: " + path.string(), ""};
  } catch (const std::exception& ex) {
    return {false, "", ex.what()};
  }
}

ToolExecutionResult ToolExecutor::tool_edit_file(const nlohmann::json& args) const {
  if (!args.contains("path") || !args["path"].is_string()) {
    return {false, "", "path is required"};
  }
  if (!args.contains("old_text") || !args["old_text"].is_string()) {
    return {false, "", "old_text is required"};
  }
  if (!args.contains("new_text") || !args["new_text"].is_string()) {
    return {false, "", "new_text is required"};
  }
  try {
    const auto path = resolve_path(args["path"].get<std::string>(), false);
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
      return {false, "", "path is a directory"};
    }
    const std::string content = read_text_file(path);
    if (content.empty() && !std::filesystem::exists(path)) {
      return {false, "", "file not found"};
    }

    const std::string old_text = args["old_text"].get<std::string>();
    const std::string new_text = args["new_text"].get<std::string>();
    if (old_text.empty()) {
      return {false, "", "old_text must not be empty"};
    }
    const std::size_t count = count_occurrences(content, old_text);
    if (count == 0) {
      return {false, "", "old_text not found in file"};
    }
    if (count > 1) {
      return {false, "", "old_text appears " + std::to_string(count) + " times; provide more context"};
    }

    const std::string replaced = replace_all(content, old_text, new_text);
    if (!write_text_file(path, replaced)) {
      return {false, "", "failed to write edited file"};
    }
    return {true, "Edited: " + path.string(), ""};
  } catch (const std::exception& ex) {
    return {false, "", ex.what()};
  }
}

ToolExecutionResult ToolExecutor::tool_list_dir(const nlohmann::json& args) const {
  std::string path = ".";
  if (args.contains("path") && args["path"].is_string()) {
    path = args["path"].get<std::string>();
  }
  try {
    const auto resolved = resolve_path(path, true);
    std::error_code ec;
    if (!std::filesystem::exists(resolved, ec)) {
      return {false, "", "directory not found"};
    }
    std::vector<std::filesystem::directory_entry> entries;
    for (const auto& entry : std::filesystem::directory_iterator(resolved, ec)) {
      if (ec) {
        break;
      }
      entries.push_back(entry);
    }
    return {true, join_dir_output(entries), ""};
  } catch (const std::exception& ex) {
    return {false, "", ex.what()};
  }
}

ToolExecutionResult ToolExecutor::tool_exec(const nlohmann::json& args) const {
  if (!args.contains("command") || !args["command"].is_string()) {
    return {false, "", "command is required"};
  }
  const std::string command = args["command"].get<std::string>();
  const std::string working_dir =
      (args.contains("working_dir") && args["working_dir"].is_string()) ? args["working_dir"].get<std::string>() : ".";
  return execute_shell(command, working_dir);
}

ToolExecutionResult ToolExecutor::tool_mkdir(const nlohmann::json& args) const {
  if (!args.contains("path") || !args["path"].is_string()) {
    return {false, "", "path is required"};
  }
  try {
    const auto path = resolve_path(args["path"].get<std::string>(), true);
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
      return {false, "", ec.message()};
    }
    return {true, "Directory created: " + path.string(), ""};
  } catch (const std::exception& ex) {
    return {false, "", ex.what()};
  }
}

ToolExecutionResult ToolExecutor::execute(const ToolCall& tool_call) const {
  if (tool_call.name == "read_file") {
    return tool_read_file(tool_call.arguments);
  }
  if (tool_call.name == "write_file") {
    return tool_write_file(tool_call.arguments);
  }
  if (tool_call.name == "append_file") {
    return tool_append_file(tool_call.arguments);
  }
  if (tool_call.name == "edit_file") {
    return tool_edit_file(tool_call.arguments);
  }
  if (tool_call.name == "list_dir") {
    return tool_list_dir(tool_call.arguments);
  }
  if (tool_call.name == "mkdir") {
    return tool_mkdir(tool_call.arguments);
  }
  if (tool_call.name == "exec") {
    return tool_exec(tool_call.arguments);
  }
  return {false, "", tool_error_text(tool_call.name, "unknown tool")};
}

}  // namespace QingLongClaw
