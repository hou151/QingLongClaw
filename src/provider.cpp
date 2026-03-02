#include "QingLongClaw/provider.h"

#include <chrono>
#include <map>
#include <regex>
#include <sstream>
#include <thread>

#include "QingLongClaw/http_client.h"
#include "QingLongClaw/util.h"

namespace QingLongClaw {

OpenAICompatProvider::OpenAICompatProvider(ResolvedModel model) : model_(std::move(model)) {}

namespace {

bool ends_with(const std::string& text, const std::string& suffix) {
  return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

bool starts_with(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool is_minmax_model_name(const std::string& model) {
  const auto lowered = to_lower(trim(model));
  if (starts_with(lowered, "minmax/") || starts_with(lowered, "minimax/")) {
    return true;
  }
  if (starts_with(lowered, "minmax-") || starts_with(lowered, "minimax-")) {
    return true;
  }
  return lowered.find("minimax") != std::string::npos || lowered.find("minmax") != std::string::npos;
}

bool is_minmax_endpoint(const std::string& endpoint) {
  const auto lowered = to_lower(endpoint);
  return contains(lowered, "minimaxi") || contains(lowered, "minimax") || contains(lowered, "minmax");
}

bool is_upstream_unavailable(const HttpResponse& response) {
  if (response.status != 502 && response.status != 503 && response.status != 504) {
    return false;
  }
  const auto body = to_lower(response.body);
  if (response.status == 503 || response.status == 504) {
    return true;
  }
  return body.find("upstream_error") != std::string::npos || body.find("temporarily unavailable") != std::string::npos;
}

bool is_timeout_error(const std::string& error) {
  const auto lowered = to_lower(error);
  return lowered.find("timeout") != std::string::npos || lowered.find("timed out") != std::string::npos;
}

HttpResponse post_with_resilience(const std::string& endpoint,
                                  const std::string& body,
                                  const std::map<std::string, std::string>& headers,
                                  const std::string& proxy,
                                  const int timeout_seconds) {
  HttpResponse response;
  constexpr int kMaxAttempts = 3;
  for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    response = HttpClient::post_json(endpoint, body, headers, proxy, timeout_seconds);
    if (!response.error.empty()) {
      if (attempt + 1 < kMaxAttempts && is_timeout_error(response.error)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250 * (attempt + 1)));
        continue;
      }
      return response;
    }
    if (attempt + 1 < kMaxAttempts && is_upstream_unavailable(response)) {
      const int backoff_ms = 500 * (1 << attempt);
      std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
      continue;
    }
    return response;
  }
  return response;
}

std::string dump_json_lossy(const nlohmann::json& value) {
  return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
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

std::string extract_error_message_from_body(const std::string& body) {
  nlohmann::json payload;
  if (parse_json_lossy(body, &payload) && payload.is_object()) {
    if (payload.contains("error") && payload["error"].is_object()) {
      const auto& error = payload["error"];
      if (error.contains("message") && error["message"].is_string()) {
        return trim(sanitize_utf8_lossy(error["message"].get<std::string>()));
      }
    }
    if (payload.contains("message") && payload["message"].is_string()) {
      return trim(sanitize_utf8_lossy(payload["message"].get<std::string>()));
    }
  }
  std::string text = trim(sanitize_utf8_lossy(body));
  if (text.size() > 300) {
    text = text.substr(0, 300) + "...";
  }
  return text;
}

bool looks_like_quota_exhausted(const HttpResponse& response) {
  const std::string lowered = to_lower(response.body);
  const bool keyword_hit =
      lowered.find("insufficient") != std::string::npos || lowered.find("insufficient_balance") != std::string::npos ||
      lowered.find("quota") != std::string::npos || lowered.find("credit") != std::string::npos ||
      lowered.find("浣欓涓嶈冻") != std::string::npos || lowered.find("閰嶉涓嶈冻") != std::string::npos;
  if (response.status == 402) {
    return true;
  }
  if ((response.status == 401 || response.status == 403 || response.status == 429) && keyword_hit) {
    return true;
  }
  return false;
}

std::string decode_xml_entities(std::string text) {
  text = replace_all(std::move(text), "&lt;", "<");
  text = replace_all(std::move(text), "&gt;", ">");
  text = replace_all(std::move(text), "&amp;", "&");
  text = replace_all(std::move(text), "&quot;", "\"");
  text = replace_all(std::move(text), "&apos;", "'");
  return text;
}

std::string strip_xml_tags(const std::string& text) {
  static const std::regex kTags(R"(<[^>]+>)");
  return trim(std::regex_replace(text, kTags, ""));
}

bool extract_minimax_tool_calls_from_text(const std::string& text, std::vector<ToolCall>* out_calls) {
  if (out_calls == nullptr || text.empty()) {
    return false;
  }

  static const std::regex kToolBlockRe(
      R"MINI(<minimax:tool_call>([\s\S]*?)</minimax:tool_call>)MINI", std::regex::icase);
  static const std::regex kInvokeRe(
      R"MINI(<invoke\s+name\s*=\s*"([^"]+)"\s*>([\s\S]*?)</invoke>)MINI", std::regex::icase);
  static const std::regex kParamRe(
      R"MINI(<parameter\s+name\s*=\s*"([^"]+)"\s*>([\s\S]*?)</parameter>)MINI", std::regex::icase);

  bool any = false;
  std::size_t tool_idx = out_calls->size();
  for (std::sregex_iterator block_it(text.begin(), text.end(), kToolBlockRe), end; block_it != end; ++block_it) {
    const std::string block = (*block_it)[1].str();
    for (std::sregex_iterator invoke_it(block.begin(), block.end(), kInvokeRe); invoke_it != end; ++invoke_it) {
      const std::string name = trim((*invoke_it)[1].str());
      const std::string invoke_body = (*invoke_it)[2].str();
      if (name.empty()) {
        continue;
      }

      ToolCall call;
      call.id = "toolcall_xml_" + std::to_string(tool_idx++);
      call.name = name;
      call.arguments = nlohmann::json::object();

      bool has_params = false;
      for (std::sregex_iterator param_it(invoke_body.begin(), invoke_body.end(), kParamRe); param_it != end; ++param_it) {
        const std::string key = trim((*param_it)[1].str());
        const std::string raw_value = (*param_it)[2].str();
        if (key.empty()) {
          continue;
        }
        has_params = true;
        call.arguments[key] = sanitize_utf8_lossy(strip_xml_tags(decode_xml_entities(raw_value)));
      }

      if (!has_params) {
        const std::string payload = sanitize_utf8_lossy(strip_xml_tags(decode_xml_entities(invoke_body)));
        if (!payload.empty()) {
          nlohmann::json parsed;
          if (parse_json_lossy(payload, &parsed)) {
            call.arguments = parsed;
          } else {
            call.arguments["input"] = payload;
          }
        }
      }

      out_calls->push_back(std::move(call));
      any = true;
    }
  }

  return any;
}

std::string strip_minimax_tool_call_blocks(const std::string& text) {
  static const std::regex kToolBlockRe(R"(<minimax:tool_call>([\s\S]*?)</minimax:tool_call>)", std::regex::icase);
  return trim(std::regex_replace(text, kToolBlockRe, ""));
}

std::string strip_think_blocks(const std::string& text) {
  if (text.empty()) {
    return text;
  }
  static const std::regex kThinkRe(R"(<think>[\s\S]*?</think>)", std::regex::icase);
  static const std::regex kThinkingRe(R"(<thinking>[\s\S]*?</thinking>)", std::regex::icase);
  std::string output = std::regex_replace(text, kThinkRe, "");
  output = std::regex_replace(output, kThinkingRe, "");
  return trim(output);
}

std::string maybe_fallback_minmax_model(const std::string& model, const HttpResponse& response) {
  if (response.status < 500 || response.status >= 600) {
    return "";
  }
  const std::string lowered_body = to_lower(response.body);
  if (lowered_body.find("not support model") == std::string::npos &&
      lowered_body.find("not support") == std::string::npos) {
    return "";
  }

  std::string candidate = model;
  static const std::regex kLightningSuffix(R"(([-_]?lightning)\b)", std::regex::icase);
  candidate = std::regex_replace(candidate, kLightningSuffix, "");
  candidate = trim(candidate);
  if (candidate.empty() || to_lower(candidate) == to_lower(model)) {
    return "";
  }
  return candidate;
}

}  // namespace

std::string OpenAICompatProvider::endpoint() const {
  std::string base = model_.api_base;
  while (!base.empty() && base.back() == '/') {
    base.pop_back();
  }
  const std::string lower_base = to_lower(base);
  if (ends_with(lower_base, "/chat/completions")) {
    return base;
  }
  if (ends_with(lower_base, "/responses")) {
    return base;
  }
  return base + "/chat/completions";
}

std::string OpenAICompatProvider::normalize_model(const std::string& model, const std::string& api_base) {
  const auto slash = model.find('/');
  if (slash == std::string::npos) {
    return model;
  }

  const std::string prefix = to_lower(model.substr(0, slash));
  const std::string lower_base = to_lower(api_base);

  if (lower_base.find("openrouter.ai") != std::string::npos) {
    return model;
  }

  if (prefix == "moonshot" || prefix == "nvidia" || prefix == "groq" || prefix == "ollama" ||
      prefix == "deepseek" || prefix == "google" || prefix == "openrouter" || prefix == "zhipu" ||
      prefix == "mistral" || prefix == "qwen" || prefix == "vllm" || prefix == "minmax" || prefix == "minimax") {
    return model.substr(slash + 1);
  }
  return model;
}

ChatResult OpenAICompatProvider::chat(const std::vector<ChatMessage>& messages,
                                      const int max_tokens,
                                      const std::optional<double>& temperature,
                                      const std::vector<ToolDefinition>& tools) const {
  ChatResult result;
  if (model_.api_base.empty()) {
    result.error = "api_base is not configured";
    return result;
  }

  const std::string endpoint_url = endpoint();
  const std::string lower_endpoint = to_lower(endpoint_url);
  const bool use_responses_api = ends_with(lower_endpoint, "/responses");

  nlohmann::json request;
  request["model"] = sanitize_utf8_lossy(normalize_model(model_.model, model_.api_base));

  if (use_responses_api) {
    std::vector<std::string> instructions_parts;
    request["input"] = nlohmann::json::array();

    for (const auto& message : messages) {
      const std::string role = to_lower(sanitize_utf8_lossy(message.role));
      if (role == "system") {
        const std::string content = sanitize_utf8_lossy(message.content);
        if (!trim(content).empty()) {
          instructions_parts.push_back(content);
        }
        continue;
      }

      if (role == "tool") {
        const std::string tool_call_id = sanitize_utf8_lossy(message.tool_call_id);
        if (!tool_call_id.empty()) {
          request["input"].push_back({
              {"type", "function_call_output"},
              {"call_id", tool_call_id},
              {"output", sanitize_utf8_lossy(message.content)},
          });
        }
        continue;
      }

      const std::string message_content = sanitize_utf8_lossy(message.content);
      if (!message_content.empty()) {
        nlohmann::json item;
        item["role"] = message.role.empty() ? "user" : sanitize_utf8_lossy(message.role);
        item["content"] = nlohmann::json::array();
        item["content"].push_back({
            {"type", "input_text"},
            {"text", message_content},
        });
        request["input"].push_back(item);
      }

      if (role == "assistant" && !message.tool_calls.empty()) {
        std::size_t call_idx = 0;
        for (const auto& call : message.tool_calls) {
          const std::string call_id = call.id.empty() ? ("toolcall_" + std::to_string(call_idx)) : sanitize_utf8_lossy(call.id);
          request["input"].push_back({
              {"type", "function_call"},
              {"call_id", call_id},
              {"name", sanitize_utf8_lossy(call.name)},
              {"arguments", dump_json_lossy(call.arguments)},
          });
          ++call_idx;
        }
      }
    }

    if (!instructions_parts.empty()) {
      std::ostringstream joined;
      for (std::size_t i = 0; i < instructions_parts.size(); ++i) {
        if (i != 0) {
          joined << "\n\n";
        }
        joined << instructions_parts[i];
      }
      request["instructions"] = sanitize_utf8_lossy(joined.str());
    }

    if (!tools.empty()) {
      request["tools"] = nlohmann::json::array();
      for (const auto& tool : tools) {
        request["tools"].push_back({
            {"type", "function"},
            {"name", sanitize_utf8_lossy(tool.name)},
            {"description", sanitize_utf8_lossy(tool.description)},
            {"parameters", tool.parameters.is_null() ? nlohmann::json::object() : tool.parameters},
        });
      }
      request["tool_choice"] = "auto";
    }

    request["max_output_tokens"] = max_tokens;
    request["stream"] = false;
    request["store"] = false;
    if (temperature.has_value()) {
      request["temperature"] = temperature.value();
    }
  } else {
    request["messages"] = nlohmann::json::array();
    const bool strip_system_role = is_minmax_model_name(model_.model) || is_minmax_endpoint(endpoint_url);
    std::vector<std::string> collected_system_messages;
    bool injected_system_prompt = false;
    for (const auto& message : messages) {
      const std::string role_name = sanitize_utf8_lossy(message.role);
      const std::string role_lower = to_lower(role_name);
      const std::string message_content = sanitize_utf8_lossy(message.content);
      if (strip_system_role && role_lower == "system") {
        if (!trim(message_content).empty()) {
          collected_system_messages.push_back(message_content);
        }
        continue;
      }

      nlohmann::json item;
      item["role"] = role_name.empty() ? "user" : role_name;
      if (!message_content.empty()) {
        std::string content = message_content;
        if (strip_system_role && role_lower == "user" && !injected_system_prompt && !collected_system_messages.empty()) {
          std::ostringstream out;
          out << "[System Instructions]\n";
          for (std::size_t i = 0; i < collected_system_messages.size(); ++i) {
            if (i != 0) {
              out << "\n\n";
            }
            out << collected_system_messages[i];
          }
          out << "\n\n[User Request]\n" << content;
          content = out.str();
          injected_system_prompt = true;
          collected_system_messages.clear();
        }
        item["content"] = sanitize_utf8_lossy(content);
      } else if (role_lower == "assistant" && !message.tool_calls.empty()) {
        item["content"] = "";
      } else {
        item["content"] = message_content;
      }

      if (!message.tool_call_id.empty()) {
        item["tool_call_id"] = sanitize_utf8_lossy(message.tool_call_id);
      }

      if (!message.tool_calls.empty()) {
        item["tool_calls"] = nlohmann::json::array();
        for (const auto& call : message.tool_calls) {
          nlohmann::json function;
          function["name"] = sanitize_utf8_lossy(call.name);
          function["arguments"] = dump_json_lossy(call.arguments);
          item["tool_calls"].push_back({
              {"id", sanitize_utf8_lossy(call.id)},
              {"type", "function"},
              {"function", function},
          });
        }
      }

      request["messages"].push_back(item);
    }
    if (strip_system_role && !collected_system_messages.empty()) {
      std::ostringstream out;
      for (std::size_t i = 0; i < collected_system_messages.size(); ++i) {
        if (i != 0) {
          out << "\n\n";
        }
        out << collected_system_messages[i];
      }
      request["messages"].insert(
          request["messages"].begin(),
          nlohmann::json{{"role", "user"}, {"content", sanitize_utf8_lossy(out.str())}});
    }

    if (!tools.empty()) {
      request["tools"] = nlohmann::json::array();
      for (const auto& tool : tools) {
        request["tools"].push_back({
            {"type", "function"},
            {"function",
             {{"name", sanitize_utf8_lossy(tool.name)},
              {"description", sanitize_utf8_lossy(tool.description)},
              {"parameters", tool.parameters.is_null() ? nlohmann::json::object() : tool.parameters}}},
        });
      }
      request["tool_choice"] = "auto";
    }

    std::string max_tokens_field = model_.max_tokens_field;
    if (max_tokens_field.empty()) {
      const std::string lower_model = to_lower(model_.model);
      if (lower_model.find("glm") != std::string::npos || lower_model.find("o1") != std::string::npos ||
          lower_model.find("gpt-5") != std::string::npos) {
        max_tokens_field = "max_completion_tokens";
      } else {
        max_tokens_field = "max_tokens";
      }
    }
    request[max_tokens_field] = max_tokens;
    if (temperature.has_value()) {
      request["temperature"] = temperature.value();
    }
  }

  std::map<std::string, std::string> headers;
  if (!model_.api_key.empty()) {
    headers["Authorization"] = "Bearer " + model_.api_key;
  }

  auto post_request = [&](const nlohmann::json& req) {
    return post_with_resilience(endpoint_url,
                                dump_json_lossy(req),
                                headers,
                                model_.proxy.empty() ? "" : model_.proxy,
                                90);
  };

  HttpResponse response = post_request(request);
  const std::string fallback_model = maybe_fallback_minmax_model(request.value("model", std::string("")), response);
  if (!fallback_model.empty()) {
    request["model"] = fallback_model;
    response = post_request(request);
  }
  if (!response.error.empty()) {
    result.error = "request failed (" + model_.model_name + " @ " + endpoint_url + "): " + response.error;
    return result;
  }
  if (response.status < 200 || response.status >= 300) {
    if (looks_like_quota_exhausted(response)) {
      const std::string message = extract_error_message_from_body(response.body);
      result.error = "quota exhausted: " + (message.empty() ? ("status " + std::to_string(response.status)) : message);
      return result;
    }
    std::ostringstream out;
    out << "request failed with status " << response.status << ": " << response.body;
    if (is_upstream_unavailable(response)) {
      out << " [upstream temporarily unavailable; retry later or switch model]";
    }
    result.error = out.str();
    return result;
  }

  if (!parse_json_lossy(response.body, &result.raw_response)) {
    result.error = "failed to parse response json";
    return result;
  }

  if (use_responses_api) {
    if (result.raw_response.contains("error") && result.raw_response["error"].is_object() &&
        !result.raw_response["error"].empty()) {
      result.error = result.raw_response["error"].value("message", "responses api returned error");
      return result;
    }
    if (!result.raw_response.contains("output") || !result.raw_response["output"].is_array()) {
      result.error = "response does not contain output";
      return result;
    }

    std::string merged_text;
    std::size_t tool_idx = 0;
    for (const auto& item : result.raw_response["output"]) {
      const std::string type = item.value("type", "");
      if (type == "message" && item.contains("content") && item["content"].is_array()) {
        for (const auto& part : item["content"]) {
          if (!part.is_object()) {
            continue;
          }
          const std::string part_type = part.value("type", "");
          if (part_type == "output_text" || part_type == "text") {
            merged_text += part.value("text", "");
          }
        }
      } else if (type == "function_call") {
        ToolCall tc;
        tc.id = item.value("call_id", item.value("id", ""));
        tc.name = item.value("name", "");

        if (item.contains("arguments")) {
          if (item["arguments"].is_string()) {
            const auto& arguments = item["arguments"].get<std::string>();
            if (!parse_json_lossy(arguments, &tc.arguments)) {
              tc.arguments = nlohmann::json{{"raw", arguments}};
            }
          } else if (!item["arguments"].is_null()) {
            tc.arguments = item["arguments"];
          }
        }
        if (tc.id.empty()) {
          tc.id = "toolcall_" + std::to_string(tool_idx);
        }
        result.tool_calls.push_back(tc);
        ++tool_idx;
      }
    }

    result.content = merged_text;
    if (result.content.empty() && result.raw_response.contains("output_text") &&
        result.raw_response["output_text"].is_string()) {
      result.content = result.raw_response["output_text"].get<std::string>();
    }
    if (result.tool_calls.empty() && !result.content.empty()) {
      std::vector<ToolCall> parsed_xml_calls;
      if (extract_minimax_tool_calls_from_text(result.content, &parsed_xml_calls)) {
        result.tool_calls = std::move(parsed_xml_calls);
        result.content = strip_minimax_tool_call_blocks(result.content);
      }
    }
    result.content = strip_think_blocks(result.content);
    if (!result.tool_calls.empty()) {
      result.finish_reason = "tool_calls";
    } else {
      result.finish_reason = result.raw_response.value("status", "");
    }
  } else {
    if (!result.raw_response.contains("choices") || !result.raw_response["choices"].is_array() ||
        result.raw_response["choices"].empty()) {
      result.error = "response does not contain choices";
      return result;
    }

    const auto& choice = result.raw_response["choices"][0];
    result.finish_reason = choice.value("finish_reason", "");

    if (!choice.contains("message") || !choice["message"].is_object()) {
      return result;
    }
    const auto& message = choice["message"];

    if (message.contains("content")) {
      if (message["content"].is_string()) {
        result.content = message["content"].get<std::string>();
      } else if (message["content"].is_array()) {
        std::string content;
        for (const auto& part : message["content"]) {
          if (part.is_object() && part.value("type", "") == "text") {
            content += part.value("text", "");
          }
        }
        result.content = content;
      }
    }

    if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
      std::size_t tool_idx = 0;
      for (const auto& call : message["tool_calls"]) {
        ToolCall tc;
        tc.id = call.value("id", "");
        if (call.contains("function") && call["function"].is_object()) {
          const auto& function = call["function"];
          tc.name = function.value("name", "");
          if (function.contains("arguments")) {
            if (function["arguments"].is_string()) {
              const auto args = function["arguments"].get<std::string>();
              if (!parse_json_lossy(args, &tc.arguments)) {
                tc.arguments = nlohmann::json{{"raw", args}};
              }
            } else if (!function["arguments"].is_null()) {
              tc.arguments = function["arguments"];
            }
          }
        }
        if (tc.id.empty()) {
          tc.id = "toolcall_" + std::to_string(tool_idx);
        }
        result.tool_calls.push_back(tc);
        ++tool_idx;
      }
    }

    if (result.tool_calls.empty() && !result.content.empty()) {
      std::vector<ToolCall> parsed_xml_calls;
      if (extract_minimax_tool_calls_from_text(result.content, &parsed_xml_calls)) {
        result.tool_calls = std::move(parsed_xml_calls);
        result.content = strip_minimax_tool_call_blocks(result.content);
      }
    }
    result.content = strip_think_blocks(result.content);
  }

  return result;
}

}  // namespace QingLongClaw
