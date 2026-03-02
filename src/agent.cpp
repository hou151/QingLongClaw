#include "QingLongClaw/agent.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <limits>
#include <sstream>
#include <unordered_set>

#include "json.hpp"
#include "QingLongClaw/skills.h"
#include "QingLongClaw/tools.h"
#include "QingLongClaw/util.h"

namespace QingLongClaw {

namespace {

std::string default_api_base_for_provider(const std::string& provider) {
  if (provider == "zhipu") {
    return "https://open.bigmodel.cn/api/paas/v4";
  }
  if (provider == "deepseek") {
    return "https://api.deepseek.com/v1";
  }
  if (provider == "qwen") {
    return "https://dashscope.aliyuncs.com/compatible-mode/v1";
  }
  if (provider == "minmax" || provider == "minimax") {
    return "https://api.minimaxi.com/v1";
  }
  return "";
}

std::string join_strings(const std::vector<std::string>& values, const std::string& separator) {
  std::ostringstream out;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) {
      out << separator;
    }
    out << values[i];
  }
  return out.str();
}

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

std::string tool_signature(const std::vector<ToolCall>& calls) {
  std::ostringstream out;
  for (const auto& call : calls) {
    out << call.name << "|" << dump_json_lossy(call.arguments) << ";";
  }
  return out.str();
}

bool looks_like_timeout(const std::string& error) {
  const std::string lowered = to_lower(error);
  return lowered.find("timeout") != std::string::npos || lowered.find("timed out") != std::string::npos;
}

std::optional<std::string> read_numeric_string_field(const nlohmann::json& object,
                                                     std::initializer_list<const char*> keys) {
  if (!object.is_object()) {
    return std::nullopt;
  }
  for (const auto* key : keys) {
    if (!object.contains(key)) {
      continue;
    }
    const auto& value = object[key];
    if (value.is_number_integer()) {
      return std::to_string(value.get<long long>());
    }
    if (value.is_number_unsigned()) {
      return std::to_string(value.get<unsigned long long>());
    }
    if (value.is_number_float()) {
      std::ostringstream out;
      out << value.get<double>();
      return out.str();
    }
    if (value.is_string()) {
      const auto text = trim(value.get<std::string>());
      if (!text.empty()) {
        return text;
      }
    }
  }
  return std::nullopt;
}

std::string format_usage_summary(const nlohmann::json& raw_response) {
  const nlohmann::json* usage = nullptr;
  if (raw_response.is_object() && raw_response.contains("usage") && raw_response["usage"].is_object()) {
    usage = &raw_response["usage"];
  } else if (raw_response.is_object() && raw_response.contains("token_usage") && raw_response["token_usage"].is_object()) {
    usage = &raw_response["token_usage"];
  }
  if (usage == nullptr) {
    return "";
  }

  const auto prompt_tokens =
      read_numeric_string_field(*usage, {"prompt_tokens", "input_tokens", "prompt_token_count"});
  const auto completion_tokens =
      read_numeric_string_field(*usage, {"completion_tokens", "output_tokens", "completion_token_count"});
  const auto total_tokens = read_numeric_string_field(*usage, {"total_tokens", "total_token_count", "tokens"});
  const auto total_cost = read_numeric_string_field(*usage, {"total_cost", "cost"});

  std::optional<std::string> remaining =
      read_numeric_string_field(*usage,
                                {"remaining_quota",
                                 "quota_remaining",
                                 "remaining_balance",
                                 "balance_remaining",
                                 "credits_remaining",
                                 "credit_balance",
                                 "remaining_tokens"});
  if (!remaining.has_value()) {
    remaining = read_numeric_string_field(raw_response,
                                          {"remaining_quota",
                                           "quota_remaining",
                                           "remaining_balance",
                                           "balance_remaining",
                                           "credits_remaining",
                                           "credit_balance",
                                           "remaining_tokens"});
  }

  if (!prompt_tokens.has_value() && !completion_tokens.has_value() && !total_tokens.has_value() &&
      !total_cost.has_value() && !remaining.has_value()) {
    return "";
  }

  std::ostringstream out;
  out << "Token usage:";
  if (prompt_tokens.has_value()) {
    out << " prompt=" << prompt_tokens.value();
  }
  if (completion_tokens.has_value()) {
    out << " completion=" << completion_tokens.value();
  }
  if (total_tokens.has_value()) {
    out << " total=" << total_tokens.value();
  }
  if (total_cost.has_value()) {
    out << " cost=" << total_cost.value();
  }
  out << " | quota_remaining=" << (remaining.has_value() ? remaining.value() : "unknown");
  return out.str();
}

std::optional<long long> read_numeric_int64_field(const nlohmann::json& object,
                                                  std::initializer_list<const char*> keys) {
  const auto text = read_numeric_string_field(object, keys);
  if (!text.has_value()) {
    return std::nullopt;
  }
  try {
    std::size_t idx = 0;
    const long double value = std::stold(text.value(), &idx);
    if (idx == 0) {
      return std::nullopt;
    }
    if (value > static_cast<long double>(std::numeric_limits<long long>::max())) {
      return std::numeric_limits<long long>::max();
    }
    if (value < static_cast<long double>(std::numeric_limits<long long>::min())) {
      return std::numeric_limits<long long>::min();
    }
    return static_cast<long long>(value);
  } catch (...) {
    return std::nullopt;
  }
}

struct UsageSnapshot {
  std::optional<long long> prompt_tokens;
  std::optional<long long> completion_tokens;
  std::optional<long long> total_tokens;
  std::optional<std::string> quota_remaining;
};

UsageSnapshot extract_usage_snapshot(const nlohmann::json& raw_response) {
  UsageSnapshot snapshot;
  const nlohmann::json* usage = nullptr;
  if (raw_response.is_object() && raw_response.contains("usage") && raw_response["usage"].is_object()) {
    usage = &raw_response["usage"];
  } else if (raw_response.is_object() && raw_response.contains("token_usage") && raw_response["token_usage"].is_object()) {
    usage = &raw_response["token_usage"];
  }
  if (usage != nullptr) {
    snapshot.prompt_tokens = read_numeric_int64_field(*usage, {"prompt_tokens", "input_tokens", "prompt_token_count"});
    snapshot.completion_tokens =
        read_numeric_int64_field(*usage, {"completion_tokens", "output_tokens", "completion_token_count"});
    snapshot.total_tokens = read_numeric_int64_field(*usage, {"total_tokens", "total_token_count", "tokens"});
    snapshot.quota_remaining = read_numeric_string_field(*usage,
                                                         {"remaining_quota",
                                                          "quota_remaining",
                                                          "remaining_balance",
                                                          "balance_remaining",
                                                          "credits_remaining",
                                                          "credit_balance",
                                                          "remaining_tokens"});
  }
  if (!snapshot.quota_remaining.has_value()) {
    snapshot.quota_remaining = read_numeric_string_field(raw_response,
                                                         {"remaining_quota",
                                                          "quota_remaining",
                                                          "remaining_balance",
                                                          "balance_remaining",
                                                          "credits_remaining",
                                                          "credit_balance",
                                                          "remaining_tokens"});
  }
  return snapshot;
}

bool request_likely_requires_tools(const std::string& message) {
  const std::string lowered = to_lower(message);
  static const std::vector<std::string> keywords = {
      "create ", "make ", "mkdir", "write ", "edit ", "modify ", "update ", "append ", "delete ", "remove ",
      "run ", "execute ", "shell", "terminal", "workspace", "file", "files", "directory", "folder", "project",
      std::string(u8"\u4ee3\u7801"),          // 代码
      std::string(u8"\u521b\u5efa"),          // 创建
      std::string(u8"\u65b0\u5efa"),          // 新建
      std::string(u8"\u5199\u5165"),          // 写入
      std::string(u8"\u4fee\u6539"),          // 修改
      std::string(u8"\u5220\u9664"),          // 删除
      std::string(u8"\u76ee\u5f55"),          // 目录
      std::string(u8"\u6587\u4ef6"),          // 文件
      std::string(u8"\u9879\u76ee"),          // 项目
      std::string(u8"\u5de5\u4f5c\u76ee\u5f55"),  // 工作目录
      std::string(u8"\u6267\u884c"),          // 执行
      std::string(u8"\u8fd0\u884c"),          // 运行
  };
  for (const auto& keyword : keywords) {
    if (lowered.find(keyword) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::string truncate_memory_text(const std::string& text, const std::size_t max_chars) {
  if (text.size() <= max_chars) {
    return text;
  }
  return text.substr(0, max_chars) + "...";
}

std::filesystem::path workspace_skills_dir(const std::filesystem::path& workspace) { return workspace / "skills"; }

std::filesystem::path legacy_templates_skills_dir(const std::filesystem::path& workspace) {
  return workspace / "templates" / "skills";
}

std::filesystem::path prefer_existing_path(const std::filesystem::path& primary,
                                           const std::filesystem::path& fallback) {
  if (std::filesystem::exists(primary)) {
    return primary;
  }
  if (std::filesystem::exists(fallback)) {
    return fallback;
  }
  return primary;
}

void ensure_file_if_missing(const std::filesystem::path& path, const std::string& content) {
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) {
    return;
  }
  write_text_file(path, content);
}

void ensure_workspace_skills_scaffold(const std::filesystem::path& workspace) {
  const auto skills_dir = workspace_skills_dir(workspace);
  std::error_code ec;
  std::filesystem::create_directories(skills_dir, ec);

  ensure_file_if_missing(skills_dir / "knowledge_urls.txt",
                         "# Add one URL per line.\n"
                         "# These sources are loaded into system prompt context.\n");
  const auto context_policy_path = skills_dir / "context_policy.json";
  ensure_file_if_missing(context_policy_path,
                         "{\n"
                         "  \"history_extra_messages\": 0,\n"
                         "  \"max_total_history_messages\": 400,\n"
                         "  \"memory_retention_tokens\": 1000000,\n"
                         "  \"retrieval_max_tokens_per_request\": 12000,\n"
                         "  \"retrieval_top_k\": 24\n"
                         "}\n");
  {
    nlohmann::json policy = nlohmann::json::object();
    const auto raw_policy = read_text_file(context_policy_path);
    if (!raw_policy.empty()) {
      nlohmann::json parsed;
      if (parse_json_lossy(raw_policy, &parsed) && parsed.is_object()) {
        policy = parsed;
      }
    }
    bool changed = false;
    const auto ensure_int = [&](const char* key, const int value) {
      if (!policy.contains(key) || !policy[key].is_number_integer()) {
        policy[key] = value;
        changed = true;
      }
    };
    ensure_int("history_extra_messages", 0);
    ensure_int("max_total_history_messages", 400);
    ensure_int("memory_retention_tokens", 1000000);
    ensure_int("retrieval_max_tokens_per_request", 12000);
    ensure_int("retrieval_top_k", 24);
    if (changed) {
      write_text_file(context_policy_path, dump_json_lossy(policy, 2));
    }
  }
  ensure_file_if_missing(skills_dir / "PROJECT_LOG.md",
                         "# PROJECT LOG\n\n"
                         "Auto-generated delivery log. Latest entries are appended by the runtime.\n");
  ensure_file_if_missing(skills_dir / "EVOLUTION.md",
                         "# EVOLUTION\n\n"
                         "Auto-generated skill evolution notes based on execution history.\n");
  ensure_file_if_missing(skills_dir / "SKILLS.md",
                         "# SKILLS MEMORY\n\n"
                         "Auto-generated patterns learned from completed tasks.\n");
  ensure_file_if_missing(skills_dir / "context_chunks.jsonl", "");

  const auto lobster_skill = skills_dir / "lobster-core" / "SKILL.md";
  ensure_file_if_missing(lobster_skill,
                         "---\n"
                         "name: lobster-core\n"
                         "description: Core project-coding workflow for the Lobster assistant. Use when handling code "
                         "analysis, implementation, debugging, build, and delivery tasks in this workspace.\n"
                         "---\n\n"
                         "Follow this workflow:\n"
                         "1. Understand user request and constraints.\n"
                         "2. Inspect workspace files before editing.\n"
                         "3. Apply minimal, testable code changes.\n"
                         "4. Run build/tests and capture errors precisely.\n"
                         "5. Record key decisions into skills/PROJECT_LOG.md via runtime auto-log.\n"
                         "6. Reuse and improve patterns captured in skills/SKILLS.md and skills/EVOLUTION.md.\n");
}

int read_context_policy_int(const std::filesystem::path& workspace,
                            const std::string& key,
                            const int fallback) {
  const auto policy_path = prefer_existing_path(workspace_skills_dir(workspace) / "context_policy.json",
                                                legacy_templates_skills_dir(workspace) / "context_policy.json");
  const auto raw = read_text_file(policy_path);
  if (raw.empty()) {
    return fallback;
  }
  nlohmann::json payload;
  if (!parse_json_lossy(raw, &payload) || !payload.is_object()) {
    return fallback;
  }
  if (!payload.contains(key) || !payload[key].is_number_integer()) {
    return fallback;
  }
  return payload[key].get<int>();
}

std::size_t resolve_history_window(const Config* config, const std::filesystem::path& workspace) {
  int base = 24;
  if (config != nullptr) {
    base = config->agents_defaults.max_history_messages;
  }
  base = std::max(4, std::min(400, base));
  const int extra = std::max(0, std::min(400, read_context_policy_int(workspace, "history_extra_messages", 0)));
  const int cap = std::max(4, std::min(800, read_context_policy_int(workspace, "max_total_history_messages", 400)));
  return static_cast<std::size_t>(std::max(4, std::min(cap, base + extra)));
}

int context_policy_int_clamped(const std::filesystem::path& workspace,
                               const std::string& key,
                               const int fallback,
                               const int minimum,
                               const int maximum) {
  const int value = read_context_policy_int(workspace, key, fallback);
  return std::max(minimum, std::min(maximum, value));
}

long long estimate_tokens_from_text(const std::string& text) {
  if (text.empty()) {
    return 0;
  }
  const long long bytes = static_cast<long long>(text.size());
  return std::max<long long>(1, (bytes + 3) / 4);
}

std::vector<std::string> extract_query_terms(const std::string& text) {
  std::vector<std::string> terms;
  std::unordered_set<std::string> seen;
  const std::string lowered = to_lower(text);

  std::string token;
  auto flush_token = [&]() {
    const auto value = trim(token);
    token.clear();
    if (value.size() < 2 || value.size() > 48) {
      return;
    }
    if (seen.insert(value).second) {
      terms.push_back(value);
    }
  };

  for (const unsigned char ch : lowered) {
    if (std::isalnum(ch) || ch == '_' || (ch & 0x80) != 0) {
      token.push_back(static_cast<char>(ch));
    } else {
      flush_token();
    }
  }
  flush_token();

  const auto full = trim(lowered);
  if (!full.empty() && full.size() <= 120 && seen.insert(full).second) {
    terms.push_back(full);
  }
  return terms;
}

int context_match_score(const std::string& context_text,
                        const std::string& lowered_query,
                        const std::vector<std::string>& query_terms) {
  if (context_text.empty()) {
    return 0;
  }
  const std::string lowered_context = to_lower(context_text);
  int score = 0;
  if (!lowered_query.empty() && lowered_query.size() >= 4 && lowered_context.find(lowered_query) != std::string::npos) {
    score += 120;
  }
  for (const auto& term : query_terms) {
    if (term.empty()) {
      continue;
    }
    std::size_t pos = 0;
    int hits = 0;
    while (hits < 3 && (pos = lowered_context.find(term, pos)) != std::string::npos) {
      ++hits;
      pos += term.size();
    }
    if (hits > 0) {
      score += hits * std::min<int>(24, static_cast<int>(term.size()) * 2);
    }
  }
  return score;
}

struct LongContextHint {
  std::string prompt;
  int selected_chunks = 0;
  long long selected_tokens = 0;
  long long retained_tokens = 0;
  long long retention_target_tokens = 0;
};

std::string normalize_task_key(const std::string& task) {
  const std::string lowered = to_lower(trim(task));
  std::string out;
  out.reserve(lowered.size());
  bool in_space = false;
  for (const unsigned char ch : lowered) {
    if (std::isspace(ch)) {
      if (!in_space) {
        out.push_back(' ');
      }
      in_space = true;
      continue;
    }
    in_space = false;
    out.push_back(static_cast<char>(ch));
  }
  out = trim(out);
  if (out.size() > 160) {
    out.resize(160);
  }
  return out;
}

bool request_looks_like_correction(const std::string& text) {
  const std::string lowered = to_lower(text);
  static const std::vector<std::string> keywords = {
      "fix", "correct", "retry", "rework", "adjust", "bug", "issue",
      std::string(u8"\u4fee\u590d"),  // 修复
      std::string(u8"\u4fee\u6b63"),  // 修正
      std::string(u8"\u66f4\u6b63"),  // 更正
      std::string(u8"\u91cd\u65b0"),  // 重新
      std::string(u8"\u9519\u8bef"),  // 错误
  };
  for (const auto& keyword : keywords) {
    if (lowered.find(keyword) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::vector<nlohmann::json> load_memory_entries_jsonl(const std::filesystem::path& path) {
  std::vector<nlohmann::json> entries;
  const auto raw = read_text_file(path);
  if (raw.empty()) {
    return entries;
  }
  const auto lines = split_lines(raw);
  entries.reserve(lines.size());
  for (const auto& raw_line : lines) {
    const auto line = trim(raw_line);
    if (line.empty()) {
      continue;
    }
    nlohmann::json entry;
    if (parse_json_lossy(line, &entry) && entry.is_object()) {
      entries.push_back(std::move(entry));
    }
  }
  return entries;
}

void save_memory_entries_jsonl(const std::filesystem::path& path, const std::vector<nlohmann::json>& entries) {
  std::ostringstream out;
  for (const auto& entry : entries) {
    if (!entry.is_object()) {
      continue;
    }
    out << dump_json_lossy(entry) << "\n";
  }
  write_text_file(path, out.str());
}

std::vector<std::string> tool_names_from_records(const std::vector<nlohmann::json>& tool_records);

long long json_int_field_or_default(const nlohmann::json& node, const char* key, const long long fallback = 0) {
  if (!node.is_object() || !node.contains(key)) {
    return fallback;
  }
  if (node[key].is_number_integer()) {
    return node[key].get<long long>();
  }
  if (node[key].is_number_unsigned()) {
    return static_cast<long long>(node[key].get<unsigned long long>());
  }
  if (node[key].is_number_float()) {
    return static_cast<long long>(node[key].get<double>());
  }
  if (node[key].is_string()) {
    try {
      return std::stoll(trim(node[key].get<std::string>()));
    } catch (...) {
      return fallback;
    }
  }
  return fallback;
}

std::string render_context_chunk_text(const std::string& request,
                                      const std::string& response,
                                      const std::vector<std::string>& tool_names,
                                      const std::string& usage_summary) {
  std::ostringstream out;
  out << "Request: " << truncate_memory_text(trim(request), 700) << "\n";
  if (!tool_names.empty()) {
    out << "Tools: " << join_strings(tool_names, ", ") << "\n";
  }
  out << "Response: " << truncate_memory_text(trim(response), 1000);
  const auto usage = trim(usage_summary);
  if (!usage.empty()) {
    out << "\nUsage: " << truncate_memory_text(usage, 220);
  }
  return out.str();
}

void persist_context_chunks(const std::filesystem::path& workspace,
                            const std::string& session_key,
                            const std::string& model_name,
                            const std::string& request,
                            const std::string& response,
                            const std::vector<nlohmann::json>& tool_records,
                            const std::string& usage_summary) {
  ensure_workspace_skills_scaffold(workspace);
  const auto skills_dir = workspace_skills_dir(workspace);
  const auto chunks_path = skills_dir / "context_chunks.jsonl";
  auto entries = load_memory_entries_jsonl(chunks_path);

  const auto tool_names = tool_names_from_records(tool_records);
  const auto text = render_context_chunk_text(request, response, tool_names, usage_summary);
  const auto token_estimate = estimate_tokens_from_text(text);
  entries.push_back(nlohmann::json{
      {"time_ms", unix_ms_now()},
      {"session_key", trim(session_key).empty() ? "default" : trim(session_key)},
      {"model", trim(model_name)},
      {"tool_names", tool_names},
      {"text", text},
      {"tokens_estimate", token_estimate},
  });

  const int retention_target = context_policy_int_clamped(workspace, "memory_retention_tokens", 1000000, 20000, 5000000);
  long long retained_tokens = 0;
  std::vector<nlohmann::json> kept;
  kept.reserve(entries.size());
  for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
    if (!it->is_object()) {
      continue;
    }
    long long tokens = json_int_field_or_default(*it, "tokens_estimate", 0);
    if (tokens <= 0) {
      tokens = estimate_tokens_from_text(it->value("text", std::string("")));
    }
    tokens = std::max<long long>(1, tokens);
    if (retained_tokens + tokens > retention_target && !kept.empty()) {
      break;
    }
    retained_tokens += tokens;
    kept.push_back(*it);
  }
  std::reverse(kept.begin(), kept.end());
  save_memory_entries_jsonl(chunks_path, kept);
}

LongContextHint build_long_context_hint(const std::filesystem::path& workspace, const std::string& user_input) {
  LongContextHint hint;
  const std::string query = trim(sanitize_utf8_lossy(user_input));
  if (query.empty()) {
    return hint;
  }

  const auto chunks_path = workspace_skills_dir(workspace) / "context_chunks.jsonl";
  const auto entries = load_memory_entries_jsonl(chunks_path);
  if (entries.empty()) {
    return hint;
  }

  const int retention_target = context_policy_int_clamped(workspace, "memory_retention_tokens", 1000000, 20000, 5000000);
  const int retrieval_budget =
      context_policy_int_clamped(workspace, "retrieval_max_tokens_per_request", 12000, 1000, 128000);
  const int retrieval_top_k = context_policy_int_clamped(workspace, "retrieval_top_k", 24, 1, 200);

  struct Candidate {
    int score = 0;
    long long time_ms = 0;
    std::string session_key;
    std::string model;
    std::string text;
    long long tokens = 0;
  };

  const std::string lowered_query = to_lower(query);
  const auto query_terms = extract_query_terms(query);
  std::vector<Candidate> ranked;
  ranked.reserve(entries.size());
  long long retained_tokens = 0;
  for (const auto& entry : entries) {
    if (!entry.is_object()) {
      continue;
    }
    Candidate candidate;
    candidate.time_ms = entry.value("time_ms", 0LL);
    candidate.session_key = trim(entry.value("session_key", std::string("")));
    candidate.model = trim(entry.value("model", std::string("")));
    candidate.text = entry.value("text", std::string(""));
    if (candidate.text.empty()) {
      continue;
    }
    candidate.tokens = json_int_field_or_default(entry, "tokens_estimate", estimate_tokens_from_text(candidate.text));
    candidate.tokens = std::max<long long>(1, candidate.tokens);
    retained_tokens += candidate.tokens;
    candidate.score = context_match_score(candidate.text, lowered_query, query_terms);
    if (candidate.score > 0) {
      ranked.push_back(std::move(candidate));
    }
  }
  hint.retained_tokens = retained_tokens;
  hint.retention_target_tokens = retention_target;

  if (ranked.empty()) {
    for (auto it = entries.rbegin(); it != entries.rend() && ranked.size() < static_cast<std::size_t>(retrieval_top_k); ++it) {
      if (!it->is_object()) {
        continue;
      }
      const auto text = it->value("text", std::string(""));
      if (text.empty()) {
        continue;
      }
      Candidate fallback;
      fallback.score = 1;
      fallback.time_ms = it->value("time_ms", 0LL);
      fallback.session_key = trim(it->value("session_key", std::string("")));
      fallback.model = trim(it->value("model", std::string("")));
      fallback.text = text;
      fallback.tokens = std::max<long long>(1, json_int_field_or_default(*it, "tokens_estimate", estimate_tokens_from_text(text)));
      ranked.push_back(std::move(fallback));
    }
  }
  if (ranked.empty()) {
    return hint;
  }

  std::sort(ranked.begin(), ranked.end(), [](const Candidate& a, const Candidate& b) {
    if (a.score != b.score) {
      return a.score > b.score;
    }
    return a.time_ms > b.time_ms;
  });

  std::ostringstream out;
  out << "Retrieved long-memory context (skills/context_chunks.jsonl):\n";
  long long used_tokens = 0;
  int selected = 0;
  const std::size_t cap = std::min<std::size_t>(ranked.size(), static_cast<std::size_t>(retrieval_top_k));
  for (std::size_t i = 0; i < cap; ++i) {
    const auto& c = ranked[i];
    if (used_tokens + c.tokens > retrieval_budget && selected > 0) {
      break;
    }
    out << "- [" << c.time_ms << "]";
    if (!c.session_key.empty()) {
      out << " session=" << c.session_key;
    }
    if (!c.model.empty()) {
      out << " model=" << c.model;
    }
    out << " score=" << c.score << "\n";
    out << "  " << truncate_memory_text(trim(c.text), 1500) << "\n";
    used_tokens += c.tokens;
    ++selected;
    if (used_tokens >= retrieval_budget) {
      break;
    }
  }

  if (selected == 0) {
    return hint;
  }
  out << "Context stats: selected_chunks=" << selected << " selected_tokens~" << used_tokens
      << " retained_tokens~" << retained_tokens << " retention_target_tokens=" << retention_target << "\n";
  hint.prompt = out.str();
  hint.selected_chunks = selected;
  hint.selected_tokens = used_tokens;
  return hint;
}

std::vector<std::string> tool_names_from_records(const std::vector<nlohmann::json>& tool_records) {
  std::vector<std::string> names;
  std::unordered_set<std::string> seen;
  names.reserve(tool_records.size());
  for (const auto& record : tool_records) {
    if (!record.is_object()) {
      continue;
    }
    const auto name = trim(record.value("name", std::string("")));
    if (name.empty() || seen.find(name) != seen.end()) {
      continue;
    }
    seen.insert(name);
    names.push_back(name);
  }
  return names;
}

std::vector<std::string> tool_names_from_entry(const nlohmann::json& entry) {
  std::vector<std::string> names;
  std::unordered_set<std::string> seen;
  auto push_name = [&](const std::string& raw) {
    const auto name = trim(raw);
    if (name.empty() || seen.find(name) != seen.end()) {
      return;
    }
    seen.insert(name);
    names.push_back(name);
  };

  if (entry.contains("tool_names") && entry["tool_names"].is_array()) {
    for (const auto& item : entry["tool_names"]) {
      if (item.is_string()) {
        push_name(item.get<std::string>());
      }
    }
  }
  if (entry.contains("tools") && entry["tools"].is_array()) {
    for (const auto& item : entry["tools"]) {
      if (item.is_object()) {
        push_name(item.value("name", std::string("")));
      }
    }
  }
  return names;
}

void write_auto_skill_markdown(const std::filesystem::path& path, const std::vector<nlohmann::json>& entries) {
  std::ostringstream out;
  out << "# SKILLS MEMORY\n\n";
  out << "Auto-generated from runtime task execution. Updated in-place for repeated/correction tasks.\n\n";

  const std::size_t start = entries.size() > 120 ? entries.size() - 120 : 0;
  for (std::size_t i = start; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    if (!entry.is_object()) {
      continue;
    }
    const auto task = truncate_memory_text(trim(entry.value("task", std::string(""))), 220);
    const auto result = truncate_memory_text(trim(entry.value("result", std::string(""))), 260);
    const auto model = trim(entry.value("model", std::string("")));
    const int revision = std::max(1, entry.value("revision", 1));
    const auto tools = tool_names_from_entry(entry);
    const int corrections =
        entry.contains("corrections") && entry["corrections"].is_array() ? static_cast<int>(entry["corrections"].size()) : 0;

    out << "## Pattern " << entry.value("updated_at_ms", entry.value("time_ms", 0LL)) << "\n";
    out << "- Task: " << (task.empty() ? "(empty)" : task) << "\n";
    out << "- Revision: " << revision << "\n";
    if (!model.empty()) {
      out << "- Model: " << model << "\n";
    }
    if (!tools.empty()) {
      out << "- Tools: " << join_strings(tools, ", ") << "\n";
    } else {
      out << "- Tools: (none)\n";
    }
    if (!result.empty()) {
      out << "- Outcome: " << result << "\n";
    }
    if (corrections > 0) {
      out << "- Corrections: " << corrections << "\n";
    }
    out << "\n";
  }
  write_text_file(path, out.str());
}

std::string build_auto_skill_memory_prompt(const std::filesystem::path& workspace) {
  const auto memory_path = prefer_existing_path(workspace_skills_dir(workspace) / "auto_memory.jsonl",
                                                legacy_templates_skills_dir(workspace) / "auto_memory.jsonl");
  const auto entries = load_memory_entries_jsonl(memory_path);
  if (entries.empty()) {
    return "";
  }

  std::ostringstream out;
  out << "\n\nRecent auto-learned execution patterns (skills/):\n";
  const std::size_t start = entries.size() > 6 ? entries.size() - 6 : 0;
  for (std::size_t i = start; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    const auto task = truncate_memory_text(trim(entry.value("task", std::string(""))), 120);
    const auto tools = tool_names_from_entry(entry);
    out << "- task: " << task;
    if (!tools.empty()) {
      out << " | tools: " << join_strings(tools, ", ");
    }
    if (entry.contains("corrections") && entry["corrections"].is_array() && !entry["corrections"].empty()) {
      out << " | corrections: " << entry["corrections"].size();
    }
    out << "\n";
  }
  return out.str();
}

std::string build_auto_skill_doc_prompt(const std::filesystem::path& workspace) {
  const auto doc_path = prefer_existing_path(workspace_skills_dir(workspace) / "SKILLS.md",
                                             legacy_templates_skills_dir(workspace) / "AUTO_SKILLS.md");
  const auto raw = read_text_file(doc_path);
  if (raw.empty()) {
    return "";
  }

  const auto lines = split_lines(raw);
  if (lines.empty()) {
    return "";
  }

  std::vector<std::string> selected;
  selected.reserve(40);
  for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
    const auto line = trim(*it);
    if (line.empty()) {
      continue;
    }
    selected.push_back(line);
    if (selected.size() >= 40) {
      break;
    }
  }
  if (selected.empty()) {
    return "";
  }
  std::reverse(selected.begin(), selected.end());

  std::ostringstream out;
  out << "\n\nAuto skills from skills/SKILLS.md (latest patterns):\n";
  for (const auto& line : selected) {
    out << line << "\n";
  }
  return out.str();
}

std::string build_manual_knowledge_prompt(const std::filesystem::path& workspace) {
  const auto knowledge_path = prefer_existing_path(workspace_skills_dir(workspace) / "knowledge_urls.txt",
                                                   legacy_templates_skills_dir(workspace) / "knowledge_urls.txt");
  const auto raw = read_text_file(knowledge_path);
  if (raw.empty()) {
    return "";
  }
  const auto lines = split_lines(raw);
  std::vector<std::string> entries;
  for (const auto& line_raw : lines) {
    const auto line = trim(line_raw);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    entries.push_back(line);
    if (entries.size() >= 20) {
      break;
    }
  }
  if (entries.empty()) {
    return "";
  }

  std::ostringstream out;
  out << "\n\nUser-maintained knowledge sources (skills/knowledge_urls.txt):\n";
  for (const auto& entry : entries) {
    out << "- " << truncate_memory_text(entry, 180) << "\n";
  }
  return out.str();
}

std::string format_local_time_from_ms(const std::int64_t time_ms) {
  const auto tp = std::chrono::system_clock::time_point(std::chrono::milliseconds(time_ms));
  return format_local_time(tp);
}

void write_evolution_markdown(const std::filesystem::path& path, std::vector<nlohmann::json> entries) {
  std::sort(entries.begin(), entries.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
    const int a_revision = std::max(1, a.value("revision", 1));
    const int b_revision = std::max(1, b.value("revision", 1));
    if (a_revision != b_revision) {
      return a_revision > b_revision;
    }
    return a.value("updated_at_ms", a.value("time_ms", 0LL)) > b.value("updated_at_ms", b.value("time_ms", 0LL));
  });

  std::ostringstream out;
  out << "# EVOLUTION\n\n";
  out << "Most reusable patterns sorted by revision count.\n\n";
  const std::size_t limit = std::min<std::size_t>(20, entries.size());
  for (std::size_t i = 0; i < limit; ++i) {
    const auto& entry = entries[i];
    if (!entry.is_object()) {
      continue;
    }
    const auto task = truncate_memory_text(trim(entry.value("task", std::string(""))), 180);
    const auto model = trim(entry.value("model", std::string("")));
    const auto tools = tool_names_from_entry(entry);
    const int revision = std::max(1, entry.value("revision", 1));
    const int corrections =
        entry.contains("corrections") && entry["corrections"].is_array() ? static_cast<int>(entry["corrections"].size()) : 0;

    out << "## Pattern " << (i + 1) << "\n";
    out << "- Task: " << (task.empty() ? "(empty)" : task) << "\n";
    out << "- Revisions: " << revision << "\n";
    if (!model.empty()) {
      out << "- Last model: " << model << "\n";
    }
    out << "- Corrections: " << corrections << "\n";
    if (!tools.empty()) {
      out << "- Stable tools: " << join_strings(tools, ", ") << "\n";
    }
    out << "\n";
  }
  write_text_file(path, out.str());
}

std::string build_workspace_skill_catalog_prompt(const std::filesystem::path& workspace) {
  SkillManager manager(workspace);
  auto skills = manager.list_installed();
  if (skills.empty()) {
    return "";
  }
  std::sort(skills.begin(), skills.end(), [](const SkillInfo& a, const SkillInfo& b) { return a.name < b.name; });

  std::ostringstream out;
  out << "\n\nWorkspace dedicated skills (skills/<name>/SKILL.md):\n";
  const std::size_t limit = std::min<std::size_t>(12, skills.size());
  for (std::size_t i = 0; i < limit; ++i) {
    out << "- " << skills[i].name;
    const auto description = trim(skills[i].description);
    if (!description.empty()) {
      out << ": " << truncate_memory_text(description, 180);
    }
    out << "\n";
  }
  out << "For code/project tasks, prioritize these local skills before improvising.\n";
  return out.str();
}

long long json_number_or_default(const nlohmann::json& node, const char* key, const long long fallback = 0) {
  if (!node.is_object() || !node.contains(key)) {
    return fallback;
  }
  if (node[key].is_number_integer()) {
    return node[key].get<long long>();
  }
  if (node[key].is_number_unsigned()) {
    return static_cast<long long>(node[key].get<unsigned long long>());
  }
  if (node[key].is_number_float()) {
    return static_cast<long long>(node[key].get<double>());
  }
  if (node[key].is_string()) {
    try {
      return std::stoll(trim(node[key].get<std::string>()));
    } catch (...) {
      return fallback;
    }
  }
  return fallback;
}

void json_add_counter(nlohmann::json* node, const char* key, const long long delta) {
  if (node == nullptr || delta == 0) {
    return;
  }
  if (!node->is_object()) {
    *node = nlohmann::json::object();
  }
  const long long current = json_number_or_default(*node, key, 0);
  (*node)[key] = current + delta;
}

void persist_context_counters(const std::filesystem::path& workspace,
                              const std::string& session_key,
                              const std::string& model_name,
                              const std::size_t history_window,
                              const std::optional<long long>& prompt_tokens,
                              const std::optional<long long>& completion_tokens,
                              const std::optional<long long>& total_tokens,
                              const std::optional<std::string>& quota_remaining) {
  ensure_workspace_skills_scaffold(workspace);
  const auto path = workspace_skills_dir(workspace) / "context_counters.json";

  nlohmann::json root = nlohmann::json::object();
  const auto raw = read_text_file(path);
  if (!raw.empty()) {
    nlohmann::json parsed;
    if (parse_json_lossy(raw, &parsed) && parsed.is_object()) {
      root = parsed;
    }
  }
  if (!root.contains("sessions") || !root["sessions"].is_object()) {
    root["sessions"] = nlohmann::json::object();
  }
  if (!root.contains("totals") || !root["totals"].is_object()) {
    root["totals"] = nlohmann::json::object();
  }
  auto& sessions = root["sessions"];
  auto& totals = root["totals"];

  const std::string normalized_session = trim(session_key).empty() ? "default" : trim(session_key);
  if (!sessions.contains(normalized_session) || !sessions[normalized_session].is_object()) {
    sessions[normalized_session] = nlohmann::json::object();
  }
  auto& session = sessions[normalized_session];
  json_add_counter(&session, "requests", 1);
  json_add_counter(&totals, "requests", 1);
  if (prompt_tokens.has_value()) {
    json_add_counter(&session, "prompt_tokens", prompt_tokens.value());
    json_add_counter(&totals, "prompt_tokens", prompt_tokens.value());
  }
  if (completion_tokens.has_value()) {
    json_add_counter(&session, "completion_tokens", completion_tokens.value());
    json_add_counter(&totals, "completion_tokens", completion_tokens.value());
  }
  if (total_tokens.has_value()) {
    json_add_counter(&session, "total_tokens", total_tokens.value());
    json_add_counter(&totals, "total_tokens", total_tokens.value());
  }
  session["last_model"] = model_name;
  session["last_history_window"] = static_cast<int>(history_window);
  session["last_updated_ms"] = unix_ms_now();
  if (quota_remaining.has_value() && !trim(quota_remaining.value()).empty()) {
    session["last_quota_remaining"] = quota_remaining.value();
    totals["last_quota_remaining"] = quota_remaining.value();
  }

  root["history_window"] = nlohmann::json{
      {"effective_messages", static_cast<int>(history_window)},
      {"policy_file", "skills/context_policy.json"},
  };
  root["updated_at_ms"] = unix_ms_now();
  write_text_file(path, dump_json_lossy(root, 2));
}

std::string build_context_counter_prompt(const std::filesystem::path& workspace) {
  const auto path = workspace_skills_dir(workspace) / "context_counters.json";
  const auto raw = read_text_file(path);
  if (raw.empty()) {
    return "";
  }
  nlohmann::json payload;
  if (!parse_json_lossy(raw, &payload) || !payload.is_object()) {
    return "";
  }
  const auto totals = payload.value("totals", nlohmann::json::object());
  const auto history_window = payload.value("history_window", nlohmann::json::object());
  const long long requests = json_number_or_default(totals, "requests", 0);
  const long long total_tokens = json_number_or_default(totals, "total_tokens", 0);
  const int effective_messages = static_cast<int>(json_number_or_default(history_window, "effective_messages", 24));

  std::ostringstream out;
  out << "\n\nContext counters (skills/context_counters.json):";
  out << " requests=" << requests;
  out << " total_tokens=" << total_tokens;
  out << " history_window_messages=" << effective_messages;
  return out.str();
}

void write_project_log_markdown(const std::filesystem::path& path, const std::vector<nlohmann::json>& entries) {
  std::ostringstream out;
  out << "# PROJECT LOG\n\n";
  out << "Auto-generated execution log from runtime sessions.\n\n";
  const std::size_t start = entries.size() > 120 ? entries.size() - 120 : 0;
  for (std::size_t i = start; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    if (!entry.is_object()) {
      continue;
    }
    const auto time_ms = entry.value("time_ms", 0LL);
    const auto session = trim(entry.value("session_key", std::string("")));
    const auto model = trim(entry.value("model", std::string("")));
    const auto request = truncate_memory_text(trim(entry.value("request", std::string(""))), 240);
    const auto response = truncate_memory_text(trim(entry.value("response", std::string(""))), 260);
    const auto tools = tool_names_from_entry(entry);
    const auto usage = trim(entry.value("usage_summary", std::string("")));

    out << "## " << time_ms << " (" << format_local_time_from_ms(time_ms) << ")\n";
    out << "- Session: " << (session.empty() ? "default" : session) << "\n";
    if (!model.empty()) {
      out << "- Model: " << model << "\n";
    }
    out << "- Request: " << (request.empty() ? "(empty)" : request) << "\n";
    if (!tools.empty()) {
      out << "- Tools: " << join_strings(tools, ", ") << "\n";
    } else {
      out << "- Tools: (none)\n";
    }
    out << "- Response: " << (response.empty() ? "(empty)" : response) << "\n";
    if (!usage.empty()) {
      out << "- Usage: " << usage << "\n";
    }
    out << "\n";
  }
  write_text_file(path, out.str());
}

void persist_project_journal(const std::filesystem::path& workspace,
                             const std::string& session_key,
                             const std::string& model_name,
                             const std::string& request,
                             const std::string& response,
                             const std::vector<nlohmann::json>& tool_records,
                             const std::string& usage_summary) {
  ensure_workspace_skills_scaffold(workspace);
  const auto skills_dir = workspace_skills_dir(workspace);
  const auto journal_path = skills_dir / "project_log.jsonl";
  const auto markdown_path = skills_dir / "PROJECT_LOG.md";

  auto entries = load_memory_entries_jsonl(journal_path);
  entries.push_back(nlohmann::json{
      {"time_ms", unix_ms_now()},
      {"session_key", session_key},
      {"model", model_name},
      {"request", truncate_memory_text(request, 900)},
      {"response", truncate_memory_text(response, 1200)},
      {"tool_names", tool_names_from_records(tool_records)},
      {"tools", tool_records},
      {"usage_summary", usage_summary},
  });
  if (entries.size() > 1200) {
    entries.erase(entries.begin(), entries.begin() + static_cast<std::ptrdiff_t>(entries.size() - 1200));
  }
  save_memory_entries_jsonl(journal_path, entries);
  write_project_log_markdown(markdown_path, entries);
}

void persist_auto_skill_memory(const std::filesystem::path& workspace,
                               const std::string& session_key,
                               const std::string& user_message,
                               const std::string& model_name,
                               const std::vector<nlohmann::json>& tool_records,
                               const std::string& output) {
  if (tool_records.empty()) {
    return;
  }

  ensure_workspace_skills_scaffold(workspace);
  const auto skills_dir = workspace_skills_dir(workspace);
  std::filesystem::create_directories(skills_dir);
  const auto memory_path = skills_dir / "auto_memory.jsonl";
  const auto markdown_path = skills_dir / "SKILLS.md";
  const auto evolution_path = skills_dir / "EVOLUTION.md";

  auto entries = load_memory_entries_jsonl(memory_path);
  const auto task_text = truncate_memory_text(user_message, 500);
  const auto task_key = normalize_task_key(task_text);
  const auto result_text = truncate_memory_text(output, 1000);
  const auto tool_names = tool_names_from_records(tool_records);
  const bool correction = request_looks_like_correction(user_message);
  const auto now_ms = unix_ms_now();

  auto make_correction_note = [&]() {
    return nlohmann::json{
        {"time_ms", now_ms},
        {"reason", truncate_memory_text(user_message, 240)},
        {"result", truncate_memory_text(output, 320)},
    };
  };

  bool updated_existing = false;
  for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
    if (!it->is_object()) {
      continue;
    }
    if (trim(it->value("task_key", std::string(""))) != task_key) {
      continue;
    }
    (*it)["updated_at_ms"] = now_ms;
    (*it)["session_key"] = session_key;
    (*it)["model"] = model_name;
    (*it)["task"] = task_text;
    (*it)["tool_names"] = tool_names;
    (*it)["tools"] = tool_records;
    (*it)["result"] = result_text;
    (*it)["revision"] = std::max(1, it->value("revision", 1)) + 1;
    if (!it->contains("corrections") || !(*it)["corrections"].is_array()) {
      (*it)["corrections"] = nlohmann::json::array();
    }
    if (correction) {
      (*it)["corrections"].push_back(make_correction_note());
    }
    updated_existing = true;
    break;
  }

  if (!updated_existing) {
    nlohmann::json entry = {
        {"time_ms", now_ms},
        {"updated_at_ms", now_ms},
        {"session_key", session_key},
        {"model", model_name},
        {"task_key", task_key},
        {"task", task_text},
        {"tool_names", tool_names},
        {"tools", tool_records},
        {"result", result_text},
        {"revision", 1},
        {"corrections", nlohmann::json::array()},
    };
    if (correction) {
      entry["corrections"].push_back(make_correction_note());
    }
    entries.push_back(std::move(entry));
  }

  if (entries.size() > 400) {
    entries.erase(entries.begin(), entries.begin() + static_cast<std::ptrdiff_t>(entries.size() - 400));
  }

  save_memory_entries_jsonl(memory_path, entries);
  write_auto_skill_markdown(markdown_path, entries);
  write_evolution_markdown(evolution_path, entries);
}

}  // namespace

AgentRuntime::AgentRuntime(Config* config, AuthStore* auth_store)
    : config_(config), auth_store_(auth_store) {}

std::string AgentRuntime::active_model_name() const {
  if (!runtime_model_override_.empty()) {
    return runtime_model_override_;
  }
  if (config_ != nullptr) {
    return config_->default_model_name();
  }
  return "unknown";
}

std::optional<std::string> AgentRuntime::handle_command(const std::string& input) {
  const std::string text = trim(input);
  if (text.empty() || text[0] != '/') {
    return std::nullopt;
  }

  std::istringstream parser(text);
  std::vector<std::string> tokens;
  std::string token;
  while (parser >> token) {
    tokens.push_back(token);
  }
  if (tokens.empty()) {
    return std::nullopt;
  }

  if (tokens[0] == "/show" && tokens.size() >= 2) {
    if (tokens[1] == "model") {
      return std::string("Current model: ") + active_model_name();
    }
    if (tokens[1] == "channel") {
      return std::string("Current channel: cli");
    }
    return std::string("Unknown show target: ") + tokens[1];
  }

  if (tokens[0] == "/list" && tokens.size() >= 2) {
    if (tokens[1] == "models") {
      std::unordered_set<std::string> uniq;
      std::vector<std::string> names;
      if (config_ != nullptr) {
        for (const auto& model : config_->model_list) {
          if (!model.model_name.empty() && !uniq.count(model.model_name)) {
            uniq.insert(model.model_name);
            names.push_back(model.model_name);
          }
        }
      }
      if (names.empty()) {
        return std::string("No models configured.");
      }
      return std::string("Available models: ") + join_strings(names, ", ");
    }
    return std::string("Unknown list target: ") + tokens[1];
  }

  if (tokens[0] == "/switch" && tokens.size() >= 4 && tokens[2] == "to") {
    if (tokens[1] == "model") {
      runtime_model_override_ = tokens[3];
      return std::string("Switched runtime model to ") + runtime_model_override_;
    }
    return std::string("Unknown switch target: ") + tokens[1];
  }

  return std::string("Unknown command: ") + tokens[0];
}

std::string AgentRuntime::system_prompt() const {
  const std::filesystem::path workspace = config_ == nullptr ? "." : config_->workspace_path();
  ensure_workspace_skills_scaffold(workspace);
  const std::filesystem::path prompt_file = workspace / "AGENT.md";
  const std::string prompt = read_text_file(prompt_file);
  const std::string memory_hint = build_auto_skill_memory_prompt(workspace);
  const std::string auto_skill_doc_hint = build_auto_skill_doc_prompt(workspace);
  const std::string knowledge_hint = build_manual_knowledge_prompt(workspace);
  const std::string skill_catalog_hint = build_workspace_skill_catalog_prompt(workspace);
  const std::string context_counter_hint = build_context_counter_prompt(workspace);
  const std::string project_continuity_hint =
      "\n\nProject continuity policy:\n"
      "- Keep reusable procedures as dedicated skills under skills/<name>/SKILL.md.\n"
      "- Runtime keeps process history in skills/PROJECT_LOG.md and evolution notes in skills/EVOLUTION.md.\n"
      "- External context expansion can be configured in skills/context_policy.json.\n";
  const std::string tool_hint =
      "\n\nTools available:\n"
      "- list_dir(path)\n"
      "- mkdir(path)\n"
      "- read_file(path, offset, max_chars)\n"
      "- write_file(path, content)\n"
      "- append_file(path, content)\n"
      "- edit_file(path, old_text, new_text)\n"
      "- exec(command, working_dir)\n"
      "Use these tools whenever local filesystem or command execution is needed.\n"
      "Never claim files were changed or commands were executed unless you actually called tools.";
  const std::string coding_workflow_hint =
      "\n\nCoding workflow policy:\n"
      "- For coding tasks, follow: understand -> plan -> implement -> verify -> summarize.\n"
      "- If user explicitly asks for plan/analysis only (for example: 'don't act yet'), provide plan only and do not run tools.\n"
      "- Prefer concise diffs and concrete verification steps.";
  if (!prompt.empty()) {
    return prompt + knowledge_hint + memory_hint + auto_skill_doc_hint + skill_catalog_hint + context_counter_hint +
           project_continuity_hint + tool_hint + coding_workflow_hint;
  }
  return "You are QingLongClaw, a practical personal AI assistant. Keep responses concise and actionable." +
         knowledge_hint + memory_hint + auto_skill_doc_hint + skill_catalog_hint + context_counter_hint +
         project_continuity_hint + tool_hint + coding_workflow_hint;
}

std::string AgentRuntime::provider_name_from_model(const std::string& model) const {
  const auto slash = model.find('/');
  if (slash == std::string::npos) {
    return to_lower(config_ == nullptr ? "" : config_->agents_defaults.provider);
  }
  return to_lower(model.substr(0, slash));
}

std::optional<ResolvedModel> AgentRuntime::resolve_model(const std::optional<std::string>& model_override) const {
  if (config_ == nullptr) {
    return std::nullopt;
  }

  std::optional<std::string> requested = model_override;
  if (!requested.has_value() && !runtime_model_override_.empty()) {
    requested = runtime_model_override_;
  }

  auto enrich_model = [&](ResolvedModel model) -> ResolvedModel {
    const std::string provider = provider_name_from_model(model.model);
    if (model.api_base.empty()) {
      model.api_base = default_api_base_for_provider(provider);
    }

    if (model.api_key.empty() && auth_store_ != nullptr) {
      std::vector<std::string> lookup_keys{provider};
      if (provider == "glm") {
        lookup_keys.push_back("zhipu");
      } else if (provider == "zhipu") {
        lookup_keys.push_back("glm");
      } else if (provider == "minimax") {
        lookup_keys.push_back("minmax");
      } else if (provider == "minmax") {
        lookup_keys.push_back("minimax");
      }
      for (const auto& key : lookup_keys) {
        auto credential = auth_store_->get(key);
        if (!credential.has_value() || credential->access_token.empty()) {
          continue;
        }
        if (credential->is_expired()) {
          continue;
        }
        model.api_key = credential->access_token;
        if (model.auth_method.empty()) {
          model.auth_method = credential->auth_method;
        }
        break;
      }
    }
    return model;
  };

  return enrich_model(config_->resolve_model(requested));
}

std::vector<ChatMessage> AgentRuntime::build_messages(const std::vector<ChatMessage>& history,
                                                      const std::string& user_input,
                                                      const std::size_t max_history_messages,
                                                      const std::string& dynamic_context_hint) const {
  std::vector<ChatMessage> messages;
  messages.push_back(ChatMessage{"system", system_prompt(), "", {}});
  if (!trim(dynamic_context_hint).empty()) {
    messages.push_back(ChatMessage{"system", dynamic_context_hint, "", {}});
  }

  const std::size_t safe_max = std::max<std::size_t>(4, std::min<std::size_t>(800, max_history_messages));
  const std::size_t start = history.size() > safe_max ? history.size() - safe_max : 0;
  for (std::size_t i = start; i < history.size(); ++i) {
    if (history[i].role == "user" || history[i].role == "assistant") {
      messages.push_back(history[i]);
    }
  }
  messages.push_back(ChatMessage{"user", user_input, "", {}});
  return messages;
}

std::string AgentRuntime::tool_result_to_message(const ToolCall& call,
                                                 const bool ok,
                                                 const std::string& text) const {
  if (ok) {
    return text;
  }
  return "[tool:" + call.name + " failed] " + text;
}

AgentResponse AgentRuntime::process_direct(const std::string& message,
                                           const std::string& session_key,
                                           const std::optional<std::string>& model_override,
                                           const bool no_history,
                                           const std::function<bool()>& should_cancel) {
  const auto is_cancelled = [&]() -> bool { return static_cast<bool>(should_cancel) && should_cancel(); };
  if (is_cancelled()) {
    return AgentResponse{"Request cancelled by user.", "", ""};
  }

  if (auto command_output = handle_command(message); command_output.has_value()) {
    return AgentResponse{*command_output, "", ""};
  }

  const auto resolved_model = resolve_model(model_override);
  if (!resolved_model.has_value()) {
    return AgentResponse{"", "failed to resolve model configuration", ""};
  }
  ResolvedModel active_model = *resolved_model;
  const std::filesystem::path workspace = config_ == nullptr ? "." : config_->workspace_path();
  ensure_workspace_skills_scaffold(workspace);
  const std::size_t effective_history_window = resolve_history_window(config_, workspace);

  if (active_model.api_base.empty()) {
    return AgentResponse{"", "api_base is empty for model " + active_model.model_name, ""};
  }
  if (active_model.api_key.empty() && active_model.auth_method.empty()) {
    return AgentResponse{"", "api_key is empty for model " + active_model.model_name, ""};
  }

  std::vector<ChatMessage> history;
  SessionStore active_sessions(workspace);
  if (!no_history) {
    history = active_sessions.load(session_key);
  }
  const LongContextHint long_context_hint = build_long_context_hint(workspace, message);
  std::vector<ChatMessage> messages =
      build_messages(history, message, effective_history_window, long_context_hint.prompt);

  OpenAICompatProvider provider(active_model);
  ToolExecutor tools(config_->workspace_path(), config_->agents_defaults.restrict_to_workspace);
  const auto tool_defs = ToolExecutor::default_definitions();
  const int model_max_tokens = std::max(256, std::min(4096, config_->agents_defaults.max_tokens));

  std::string output;
  const int configured_max_iterations = config_->agents_defaults.max_tool_iterations;
  const bool unlimited_iterations = configured_max_iterations <= 0;
  const int max_iterations =
      unlimited_iterations ? std::numeric_limits<int>::max() : std::max(1, std::min(100000, configured_max_iterations));
  bool completed = false;
  std::string previous_tool_signature;
  std::string last_tool_output;
  std::string latest_usage_summary;
  long long prompt_tokens_sum = 0;
  long long completion_tokens_sum = 0;
  long long total_tokens_sum = 0;
  bool has_prompt_tokens = false;
  bool has_completion_tokens = false;
  bool has_total_tokens = false;
  std::optional<std::string> latest_quota_remaining;
  const bool requires_tools = request_likely_requires_tools(message);
  bool retried_with_tool_requirement = false;
  std::vector<nlohmann::json> tool_records;

  int iteration = 0;
  for (; unlimited_iterations || iteration < max_iterations; ++iteration) {
    if (is_cancelled()) {
      output = "Request cancelled by user.";
      completed = true;
      break;
    }

    ChatResult response =
        provider.chat(messages, model_max_tokens, config_->agents_defaults.temperature, tool_defs);
    if (is_cancelled()) {
      output = "Request cancelled by user.";
      completed = true;
      break;
    }
    if (!response.ok()) {

      const bool timeout_error = looks_like_timeout(response.error);
      if (timeout_error) {
        ChatResult fallback =
            provider.chat(messages, model_max_tokens, config_->agents_defaults.temperature, {});
        if (fallback.ok()) {
          output = trim(fallback.content);
          completed = true;
          break;
        }
      }
      if (!last_tool_output.empty()) {
        output = last_tool_output +
                 "\n\n[System] LLM timeout after tool execution. Returned last tool output as fallback.";
        completed = true;
        break;
      }
      if (timeout_error) {
        return AgentResponse{
            "",
            response.error +
                ". Hint: check API key, api_base/proxy, and outbound network access from the host.",
            "",
        };
      }
      return AgentResponse{"", response.error, ""};
    }
    const std::string usage_summary = format_usage_summary(response.raw_response);
    if (!usage_summary.empty()) {
      latest_usage_summary = usage_summary;
    }
    const auto usage_snapshot = extract_usage_snapshot(response.raw_response);
    if (usage_snapshot.prompt_tokens.has_value()) {
      has_prompt_tokens = true;
      prompt_tokens_sum += usage_snapshot.prompt_tokens.value();
    }
    if (usage_snapshot.completion_tokens.has_value()) {
      has_completion_tokens = true;
      completion_tokens_sum += usage_snapshot.completion_tokens.value();
    }
    if (usage_snapshot.total_tokens.has_value()) {
      has_total_tokens = true;
      total_tokens_sum += usage_snapshot.total_tokens.value();
    }
    if (usage_snapshot.quota_remaining.has_value() && !trim(usage_snapshot.quota_remaining.value()).empty()) {
      latest_quota_remaining = usage_snapshot.quota_remaining;
    }

    ChatMessage assistant_msg;
    assistant_msg.role = "assistant";
    assistant_msg.content = response.content;
    assistant_msg.tool_calls = response.tool_calls;
    messages.push_back(assistant_msg);

    if (response.tool_calls.empty()) {
      if (requires_tools && !retried_with_tool_requirement) {
        retried_with_tool_requirement = true;
        messages.push_back(ChatMessage{
            "system",
            "User requested filesystem/command operations. You MUST use tools to perform real workspace actions. "
            "Do not claim files were created or modified unless you actually called tools in this turn.",
            "",
            {}});
        continue;
      }
      if (requires_tools) {
        const std::string raw_reply = trim(response.content);
        output = "No tool call was produced, so no workspace changes were made.";
        if (!raw_reply.empty()) {
          output += "\n\nModel reply:\n" + raw_reply;
        }
        completed = true;
        break;
      }
      output = trim(response.content);
      completed = true;
      break;
    }

    const std::string current_signature = tool_signature(response.tool_calls);
    if (!previous_tool_signature.empty() && current_signature == previous_tool_signature) {
      output = "Tool calls are repeating. Stopped to avoid endless loop.\n" + last_tool_output;
      completed = true;
      break;
    }
    previous_tool_signature = current_signature;

    for (const auto& call : response.tool_calls) {
      if (is_cancelled()) {
        output = "Request cancelled by user.";
        completed = true;
        break;
      }
      const auto exec_result = tools.execute(call);
      ChatMessage tool_message;
      tool_message.role = "tool";
      tool_message.tool_call_id = call.id;
      tool_message.content = tool_result_to_message(
          call, exec_result.ok, exec_result.ok ? exec_result.output : exec_result.error);
      messages.push_back(tool_message);
      last_tool_output = tool_message.content;

      nlohmann::json record{
          {"name", call.name},
          {"arguments", call.arguments},
          {"ok", exec_result.ok},
          {"output", truncate_memory_text(exec_result.ok ? exec_result.output : exec_result.error, 600)},
      };
      tool_records.push_back(std::move(record));
    }
    if (completed) {
      break;
    }
  }

  if (!completed && output.empty()) {
    ChatResult finalize =
        provider.chat(messages, model_max_tokens, config_->agents_defaults.temperature, {});
    if (finalize.ok() && !trim(finalize.content).empty()) {
      output = trim(finalize.content);
      completed = true;
    } else {
      if (unlimited_iterations) {
        output = "No final response content was produced after tool execution.";
      } else {
        output = "Reached max tool iterations (" + std::to_string(max_iterations) +
                 "). Partial tool results were returned to model. "
                 "Set agents.defaults.max_tool_iterations=0 for unlimited iterations.";
      }
    }
  }

  if (output.empty()) {
    if (!trim(last_tool_output).empty()) {
      output = last_tool_output;
    } else {
      output = "I completed processing but have no response content.";
    }
  }

  if (!no_history) {
    try {
      history.push_back(ChatMessage{"user", sanitize_utf8_lossy(message), "", {}});
      history.push_back(ChatMessage{"assistant", sanitize_utf8_lossy(output), "", {}});
      const bool saved = active_sessions.save(session_key, history);
      if (!saved) {
        std::cerr << "[agent] warning: failed to save session history for " << session_key << "\n";
      }
    } catch (const std::exception& ex) {
      std::cerr << "[agent] warning: save session history threw exception: " << ex.what() << "\n";
    } catch (...) {
      std::cerr << "[agent] warning: save session history threw unknown exception\n";
    }
  }

  if (config_ != nullptr && !tool_records.empty()) {
    try {
      persist_auto_skill_memory(workspace,
                                session_key,
                                sanitize_utf8_lossy(message),
                                sanitize_utf8_lossy(active_model.model_name),
                                tool_records,
                                sanitize_utf8_lossy(output));
    } catch (const std::exception& ex) {
      std::cerr << "[agent] warning: persist auto-skill memory threw exception: " << ex.what() << "\n";
    } catch (...) {
      std::cerr << "[agent] warning: persist auto-skill memory threw unknown exception\n";
    }
  }
  std::string final_usage_summary = sanitize_utf8_lossy(latest_usage_summary);
  if (final_usage_summary.empty()) {
    final_usage_summary = "Token usage: unavailable";
  }
  final_usage_summary += " | history_window_messages=" + std::to_string(effective_history_window);
  final_usage_summary += " | retrieved_context_chunks=" + std::to_string(long_context_hint.selected_chunks);
  final_usage_summary += " | retrieved_context_tokens~" + std::to_string(long_context_hint.selected_tokens);
  final_usage_summary += " | retained_context_tokens~" + std::to_string(long_context_hint.retained_tokens);
  final_usage_summary += " | retention_target_tokens=" + std::to_string(long_context_hint.retention_target_tokens);

  if (config_ != nullptr) {
    try {
      persist_project_journal(workspace,
                              session_key,
                              sanitize_utf8_lossy(active_model.model_name),
                              sanitize_utf8_lossy(message),
                              sanitize_utf8_lossy(output),
                              tool_records,
                              final_usage_summary);
    } catch (const std::exception& ex) {
      std::cerr << "[agent] warning: persist project journal threw exception: " << ex.what() << "\n";
    } catch (...) {
      std::cerr << "[agent] warning: persist project journal threw unknown exception\n";
    }

    try {
      persist_context_chunks(workspace,
                             session_key,
                             sanitize_utf8_lossy(active_model.model_name),
                             sanitize_utf8_lossy(message),
                             sanitize_utf8_lossy(output),
                             tool_records,
                             final_usage_summary);
    } catch (const std::exception& ex) {
      std::cerr << "[agent] warning: persist context chunks threw exception: " << ex.what() << "\n";
    } catch (...) {
      std::cerr << "[agent] warning: persist context chunks threw unknown exception\n";
    }

    try {
      persist_context_counters(workspace,
                               session_key,
                               sanitize_utf8_lossy(active_model.model_name),
                               effective_history_window,
                               has_prompt_tokens ? std::optional<long long>(prompt_tokens_sum) : std::nullopt,
                               has_completion_tokens ? std::optional<long long>(completion_tokens_sum) : std::nullopt,
                               has_total_tokens ? std::optional<long long>(total_tokens_sum) : std::nullopt,
                               latest_quota_remaining);
    } catch (const std::exception& ex) {
      std::cerr << "[agent] warning: persist context counters threw exception: " << ex.what() << "\n";
    } catch (...) {
      std::cerr << "[agent] warning: persist context counters threw unknown exception\n";
    }
  }

  return AgentResponse{sanitize_utf8_lossy(output), "", final_usage_summary};
}

}  // namespace QingLongClaw


