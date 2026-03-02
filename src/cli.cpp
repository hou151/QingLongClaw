#include "QingLongClaw/cli.h"

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "QingLongClaw/agent.h"
#include "QingLongClaw/auth.h"
#include "QingLongClaw/config.h"
#include "QingLongClaw/cron_service.h"
#include "QingLongClaw/gateway.h"
#include "QingLongClaw/model_preset.h"
#include "QingLongClaw/skills.h"
#include "QingLongClaw/util.h"
#include "QingLongClaw/version.h"

namespace QingLongClaw {

namespace {

std::vector<std::string> args_to_vector(int argc, char** argv) {
  std::vector<std::string> args;
  args.reserve(static_cast<std::size_t>(argc));
  for (int i = 0; i < argc; ++i) {
    args.emplace_back(argv[i] == nullptr ? "" : argv[i]);
  }
  return args;
}

bool has_flag(const std::vector<std::string>& args, const std::string& flag_a, const std::string& flag_b = "") {
  for (const auto& arg : args) {
    if (arg == flag_a || (!flag_b.empty() && arg == flag_b)) {
      return true;
    }
  }
  return false;
}

std::optional<std::string> find_option(const std::vector<std::string>& args,
                                       const std::string& option_a,
                                       const std::string& option_b = "") {
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == option_a || (!option_b.empty() && args[i] == option_b)) {
      if (i + 1 < args.size()) {
        return args[i + 1];
      }
    }
  }
  return std::nullopt;
}

std::filesystem::path find_existing_path_from_roots(
    const std::vector<std::filesystem::path>& relative_candidates) {
  auto cursor = std::filesystem::current_path();
  for (int depth = 0; depth < 6; ++depth) {
    for (const auto& relative : relative_candidates) {
      const auto candidate = cursor / relative;
      std::error_code ec;
      if (std::filesystem::exists(candidate, ec)) {
        return candidate;
      }
    }
    if (!cursor.has_parent_path()) {
      break;
    }
    cursor = cursor.parent_path();
  }
  return {};
}

std::filesystem::path find_picoclaw_workspace_template_dir() {
  return find_existing_path_from_roots({
      "picoclaw/workspace",
      "../picoclaw/workspace",
  });
}

std::filesystem::path find_builtin_skills_dir() {
  auto path = find_existing_path_from_roots({
      "picoclaw/workspace/skills",
      "../picoclaw/workspace/skills",
  });
  if (!path.empty()) {
    return path;
  }
  return find_existing_path_from_roots({
      "templates/workspace/skills",
      "../templates/workspace/skills",
  });
}

std::filesystem::path find_local_template_dir() {
  return find_existing_path_from_roots({
      "templates/workspace",
      "../templates/workspace",
  });
}

void write_default_workspace_files(const std::filesystem::path& workspace) {
  write_text_file(workspace / "AGENT.md",
                  "# AGENT\n\n"
                  "You are QingLongClaw, an efficient AI coding assistant.\n"
                  "Be concise, safe, and practical.\n");
  write_text_file(workspace / "IDENTITY.md",
                  "# Identity\n\n"
                  "Project: QingLongClaw\n"
                  "Language: C++\n"
                  "Runtime: Linux x86_64 and aarch64 compatible\n");
  write_text_file(workspace / "SOUL.md",
                  "# Soul\n\n"
                  "Keep responses clear and execution-focused.\n");
  write_text_file(workspace / "memory" / "MEMORY.md",
                  "# Memory\n\n"
                  "- Track decisions and key context here.\n");
  write_text_file(workspace / "skills" / "knowledge_urls.txt",
                  "# Add one URL per line.\n"
                  "# These entries are injected as lightweight references.\n");
  write_text_file(workspace / "skills" / "context_policy.json",
                  "{\n"
                  "  \"history_extra_messages\": 0,\n"
                  "  \"max_total_history_messages\": 400,\n"
                  "  \"memory_retention_tokens\": 1000000,\n"
                  "  \"retrieval_max_tokens_per_request\": 12000,\n"
                  "  \"retrieval_top_k\": 24\n"
                  "}\n");
  write_text_file(workspace / "skills" / "PROJECT_LOG.md",
                  "# PROJECT LOG\n\n"
                  "Auto-generated execution log from runtime sessions.\n");
  write_text_file(workspace / "skills" / "EVOLUTION.md",
                  "# EVOLUTION\n\n"
                  "Auto-generated skill evolution notes.\n");
  write_text_file(workspace / "skills" / "context_chunks.jsonl", "");
  write_text_file(workspace / "skills" / "lobster-core" / "SKILL.md",
                  "---\n"
                  "name: lobster-core\n"
                  "description: Core coding workflow for Lobster. Use for implementation, debugging, build, and delivery tasks in this workspace.\n"
                  "---\n\n"
                  "1. Read request and constraints.\n"
                  "2. Inspect relevant files before patching.\n"
                  "3. Make minimal, testable edits.\n"
                  "4. Run build/tests and capture exact failures.\n"
                  "5. Keep project continuity via skills/PROJECT_LOG.md and skills/EVOLUTION.md.\n");
}

void print_version() { std::cout << version_text(false) << "\n"; }

void print_help() {
  std::cout << "QingLongClaw - C++ Task Automation Claw\n\n";
  std::cout << "Usage: qinglongclaw <command>\n";
  std::cout << "(binary name: qinglongclaw)\n\n";
  std::cout << "Commands:\n";
  std::cout << "  onboard     Initialize QingLongClaw configuration and workspace\n";
  std::cout << "  agent       Interact with the agent directly\n";
  std::cout << "  gateway     Start QingLongClaw gateway\n";
  std::cout << "  status      Show QingLongClaw status\n";
  std::cout << "  migrate     Migrate configuration from picoclaw\n";
  std::cout << "  auth        Manage authentication (login, logout, status)\n";
  std::cout << "  cron        Manage scheduled tasks\n";
  std::cout << "  skills      Manage skills (install, list, remove)\n";
  std::cout << "  version     Show version information (--detail/--json)\n";
}

void print_auth_help() {
  std::cout << "Auth commands:\n";
  std::cout << "  login --provider <name> [--token <api_key>]    Save provider token\n";
  std::cout << "  logout [--provider <name>] Remove provider token(s)\n";
  std::cout << "  status                     Show auth status\n";
  std::cout << "Providers: zhipu(glm), deepseek, qwen, minmax(minimax)\n";
}

void print_skills_help() {
  std::cout << "Skills commands:\n";
  std::cout << "  list\n";
  std::cout << "  install <repo>\n";
  std::cout << "  remove <name>\n";
  std::cout << "  install-builtin\n";
  std::cout << "  list-builtin\n";
  std::cout << "  search\n";
  std::cout << "  show <name>\n";
}

void print_cron_help() {
  std::cout << "Cron commands:\n";
  std::cout << "  list\n";
  std::cout << "  add -n <name> -m <message> (-e <seconds> | -c <expr>) [--deliver --channel <name> --to <id>]\n";
  std::cout << "  remove <id>\n";
  std::cout << "  enable <id>\n";
  std::cout << "  disable <id>\n";
}

Config load_config() {
  Config config = Config::load(default_config_path());
  apply_supported_model_preset(&config, true);
  return config;
}

bool save_config(const Config& input) {
  Config config = input;
  apply_supported_model_preset(&config, true);
  return config.save(default_config_path());
}

int cmd_onboard() {
  const auto config_path = default_config_path();
  std::error_code ec;
  if (std::filesystem::exists(config_path, ec)) {
    std::cout << "Config already exists at " << config_path.string() << "\n";
    std::cout << "Overwrite? (y/n): ";
    std::string answer;
    std::getline(std::cin, answer);
    if (trim(to_lower(answer)) != "y") {
      std::cout << "Aborted.\n";
      return 0;
    }
  }

  Config config = Config::default_config();
  apply_supported_model_preset(&config, false);
  if (!config.save(config_path)) {
    std::cerr << "Failed to write config: " << config_path.string() << "\n";
    return 1;
  }

  const auto workspace = std::filesystem::path(config.workspace_path());
  std::filesystem::create_directories(workspace, ec);

  bool copied_templates = false;
  const auto picoclaw_workspace = find_picoclaw_workspace_template_dir();
  if (!picoclaw_workspace.empty()) {
    copied_templates = copy_directory_recursive(picoclaw_workspace, workspace);
  }
  if (!copied_templates) {
    const auto local_templates = find_local_template_dir();
    if (!local_templates.empty()) {
      copied_templates = copy_directory_recursive(local_templates, workspace);
    }
  }
  if (!copied_templates) {
    write_default_workspace_files(workspace);
  }

  std::cout << "QingLongClaw is ready.\n";
  std::cout << "Config: " << config_path.string() << "\n";
  std::cout << "Workspace: " << workspace.string() << "\n\n";
  std::cout << "Next steps:\n";
  std::cout << "  1. Configure API keys (config file or web console).\n";
  std::cout << "  2. CLI chat: qinglongclaw agent -m \"Hello\"\n";
  std::cout << "  3. Web chat: qinglongclaw gateway, then open http://<ip>:18790/\n";
  return 0;
}

int cmd_status() {
  const Config config = load_config();
  const auto config_path = default_config_path();
  const auto workspace = std::filesystem::path(config.workspace_path());

  print_version();
  std::cout << "Config: " << config_path.string();
  std::cout << (std::filesystem::exists(config_path) ? " [OK]\n" : " [MISSING]\n");
  std::cout << "Workspace: " << workspace.string();
  std::cout << (std::filesystem::exists(workspace) ? " [OK]\n" : " [MISSING]\n");
  std::cout << "Model: " << config.default_model_name() << "\n";
  std::cout << "Context history window: " << config.agents_defaults.max_history_messages
            << " messages (+ skills/context_policy.json)\n";

  if (config.raw.contains("providers") && config.raw["providers"].is_object()) {
    std::cout << "Providers:\n";
    for (auto it = config.raw["providers"].begin(); it != config.raw["providers"].end(); ++it) {
      if (!it.value().is_object()) {
        continue;
      }
      const std::string api_key = it.value().value("api_key", "");
      const std::string auth_method = it.value().value("auth_method", "");
      const bool enabled = !api_key.empty() || !auth_method.empty();
      std::cout << "  - " << it.key() << ": " << (enabled ? "configured" : "not set") << "\n";
    }
  }

  AuthStore auth(default_auth_store_path());
  auth.load();
  const auto credentials = auth.list();
  if (!credentials.empty()) {
    std::cout << "Auth:\n";
    for (const auto& credential : credentials) {
      std::string status = "active";
      if (credential.is_expired()) {
        status = "expired";
      } else if (credential.needs_refresh()) {
        status = "needs refresh";
      }
      std::cout << "  - " << credential.provider << " (" << credential.auth_method << "): " << status << "\n";
    }
  }
  return 0;
}

int cmd_version(const std::vector<std::string>& args) {
  if (has_flag(args, "--json")) {
    std::cout << version_payload().dump(2) << "\n";
    return 0;
  }
  const bool detailed = has_flag(args, "--detail", "-d");
  std::cout << version_text(detailed) << "\n";
  return 0;
}

int cmd_agent(const std::vector<std::string>& args) {
  std::string message;
  std::string session_key = "cli:default";
  std::optional<std::string> model_override = std::nullopt;

  for (std::size_t i = 0; i < args.size(); ++i) {
    if ((args[i] == "-m" || args[i] == "--message") && i + 1 < args.size()) {
      message = args[i + 1];
      ++i;
      continue;
    }
    if ((args[i] == "-s" || args[i] == "--session") && i + 1 < args.size()) {
      session_key = args[i + 1];
      ++i;
      continue;
    }
    if ((args[i] == "--model" || args[i] == "-model") && i + 1 < args.size()) {
      model_override = args[i + 1];
      ++i;
      continue;
    }
  }

  Config config = load_config();
  AuthStore auth(default_auth_store_path());
  auth.load();
  AgentRuntime runtime(&config, &auth);

  if (!message.empty()) {
    const auto response = runtime.process_direct(message, session_key, model_override);
    if (!response.ok()) {
      std::cerr << "Error: " << response.error << "\n";
      return 1;
    }
    std::cout << response.content << "\n";
    return 0;
  }

  std::cout << "Interactive mode (Ctrl+C or type exit)\n";
  while (true) {
    std::cout << "You: ";
    std::string input;
    if (!std::getline(std::cin, input)) {
      std::cout << "\nBye.\n";
      break;
    }
    input = trim(input);
    if (input.empty()) {
      continue;
    }
    if (input == "exit" || input == "quit") {
      std::cout << "Bye.\n";
      break;
    }
    const auto response = runtime.process_direct(input, session_key, model_override);
    if (!response.ok()) {
      std::cerr << "Error: " << response.error << "\n";
      continue;
    }
    std::cout << "QingLongClaw: " << response.content << "\n";
  }
  return 0;
}

int cmd_gateway(const std::vector<std::string>& args) {
  Config config = load_config();
  AuthStore auth(default_auth_store_path());
  auth.load();
  const bool debug = has_flag(args, "--debug", "-d");
  return run_gateway(&config, &auth, debug);
}

int cmd_migrate() {
  const auto src_root = std::filesystem::path(home_dir()) / ".picoclaw";
  const auto dst_root = config_root();
  const auto src_config = src_root / "config.json";
  const auto dst_config = dst_root / "config.json";
  const auto src_workspace = src_root / "workspace";
  const auto dst_workspace = dst_root / "workspace";

  std::error_code ec;
  if (!std::filesystem::exists(src_config, ec)) {
    std::cerr << "Source config not found: " << src_config.string() << "\n";
    return 1;
  }

  std::string config_text = read_text_file(src_config);
  if (config_text.empty()) {
    std::cerr << "Failed to read source config\n";
    return 1;
  }
  config_text = replace_all(config_text, "~/.picoclaw/workspace", "~/.qinglongclaw/workspace");
  if (!write_text_file(dst_config, config_text)) {
    std::cerr << "Failed to write target config: " << dst_config.string() << "\n";
    return 1;
  }
  {
    Config migrated = load_config();
    apply_supported_model_preset(&migrated, true);
    save_config(migrated);
  }

  if (std::filesystem::exists(src_workspace, ec)) {
    copy_directory_recursive(src_workspace, dst_workspace);
  }

  std::cout << "Migration complete.\n";
  std::cout << "Config: " << dst_config.string() << "\n";
  std::cout << "Workspace: " << dst_workspace.string() << "\n";
  return 0;
}

int cmd_auth(const std::vector<std::string>& args) {
  if (args.empty()) {
    print_auth_help();
    return 0;
  }

  AuthStore auth(default_auth_store_path());
  auth.load();

  const std::string subcommand = args[0];
  if (subcommand == "login") {
    const auto provider_opt = find_option(args, "--provider", "-p");
    if (!provider_opt.has_value()) {
      std::cerr << "Error: --provider is required\n";
      print_auth_help();
      return 1;
    }
    const std::string provider = to_lower(*provider_opt);
    if (has_flag(args, "--device-code")) {
      std::cout << "Device code flow is not implemented in this C++ version. Falling back to token input.\n";
    }
    if (provider != "zhipu" && provider != "glm" && provider != "deepseek" && provider != "qwen" &&
        provider != "minmax" && provider != "minimax") {
      std::cerr << "Unsupported provider. Use zhipu(glm), deepseek, qwen, minmax(minimax).\n";
      return 1;
    }
    std::string token = trim(find_option(args, "--token").value_or(""));
    if (token.empty()) {
      std::cout << "Paste token for provider '" << provider << "': ";
      std::getline(std::cin, token);
      token = trim(token);
    }
    if (token.empty()) {
      std::cerr << "Token is empty.\n";
      return 1;
    }

    AuthCredential credential;
    credential.provider = (provider == "glm") ? "zhipu" : ((provider == "minimax") ? "minmax" : provider);
    credential.auth_method = "token";
    credential.access_token = token;
    credential.updated_at_ms = unix_ms_now();
    auth.upsert(credential);
    if (!auth.save()) {
      std::cerr << "Failed to save auth store.\n";
      return 1;
    }

    Config config = load_config();
    apply_supported_model_preset(&config, true);
    if (provider == "zhipu" || provider == "glm") {
      config.agents_defaults.model_name = "glm-4.7";
      update_model_secret(&config, "glm-4.7", token);
    } else if (provider == "deepseek") {
      config.agents_defaults.model_name = "deepseek-chat";
      update_model_secret(&config, "deepseek-chat", token);
    } else if (provider == "qwen") {
      config.agents_defaults.model_name = "qwen-plus";
      update_model_secret(&config, "qwen-plus", token);
    } else if (provider == "minmax" || provider == "minimax") {
      config.agents_defaults.model_name = "minimax-m2.1";
      update_model_secret(&config, "minimax-m2.1", token);
      auto& providers = config.raw["providers"];
      if (!providers.is_object()) {
        providers = nlohmann::json::object();
      }
      auto& minmax = providers["minmax"];
      if (!minmax.is_object()) {
        minmax = nlohmann::json::object();
      }
      minmax["api_key"] = token;
      if (!minmax.contains("api_base") || !minmax["api_base"].is_string() ||
          trim(minmax["api_base"].get<std::string>()).empty()) {
        minmax["api_base"] = "https://api.minimaxi.com/v1";
      }
      if (!minmax.contains("proxy")) {
        minmax["proxy"] = "";
      }
    }
    config.agents_defaults.legacy_model = config.agents_defaults.model_name;
    save_config(config);

    std::cout << "Login successful for " << provider << ".\n";
    return 0;
  }

  if (subcommand == "logout") {
    const auto provider_opt = find_option(args, "--provider", "-p");
    if (provider_opt.has_value()) {
      std::string provider = to_lower(*provider_opt);
      if (provider == "glm") {
        provider = "zhipu";
      } else if (provider == "minimax") {
        provider = "minmax";
      }
      if (!auth.remove(provider)) {
        std::cout << "No credential found for " << *provider_opt << ".\n";
      } else {
        std::cout << "Logged out from " << *provider_opt << ".\n";
      }
    } else {
      auth.clear();
      std::cout << "Logged out from all providers.\n";
    }
    auth.save();

    Config config = load_config();
    if (config.raw.contains("providers") && config.raw["providers"].is_object()) {
      for (auto it = config.raw["providers"].begin(); it != config.raw["providers"].end(); ++it) {
        if (it.value().is_object() && it.value().contains("auth_method")) {
          it.value()["auth_method"] = "";
        }
      }
    }
    for (auto& model : config.model_list) {
      model.auth_method.clear();
    }
    save_config(config);
    return 0;
  }

  if (subcommand == "status") {
    const auto credentials = auth.list();
    if (credentials.empty()) {
      std::cout << "No authenticated providers.\n";
      return 0;
    }
    for (const auto& credential : credentials) {
      std::string status = "active";
      if (credential.is_expired()) {
        status = "expired";
      } else if (credential.needs_refresh()) {
        status = "needs refresh";
      }
      std::cout << credential.provider << " (" << credential.auth_method << "): " << status << "\n";
    }
    return 0;
  }

  if (subcommand == "models") {
    std::cout << "Supported models:\n";
    for (const auto& preset : supported_models()) {
      std::cout << "- " << preset.model_name << " (" << preset.model << ")\n";
    }
    return 0;
  }

  print_auth_help();
  return 1;
}

int cmd_skills(const std::vector<std::string>& args) {
  if (args.empty()) {
    print_skills_help();
    return 0;
  }
  Config config = load_config();
  SkillManager manager(config.workspace_path());

  const std::string subcommand = args[0];
  if (subcommand == "list") {
    const auto skills = manager.list_installed();
    if (skills.empty()) {
      std::cout << "No skills installed.\n";
      return 0;
    }
    for (const auto& skill : skills) {
      std::cout << "- " << skill.name;
      if (!skill.description.empty()) {
        std::cout << ": " << skill.description;
      }
      std::cout << "\n";
    }
    return 0;
  }

  if (subcommand == "install") {
    if (args.size() < 2) {
      std::cerr << "Usage: qinglongclaw skills install <repo>\n";
      return 1;
    }
    if (args[1] == "--registry") {
      std::cerr << "Registry install is not implemented in this C++ version yet.\n";
      return 1;
    }
    std::string error;
    if (!manager.install_from_github(args[1], &error)) {
      std::cerr << "Failed to install skill: " << error << "\n";
      return 1;
    }
    std::cout << "Skill installed: " << std::filesystem::path(args[1]).filename().string() << "\n";
    return 0;
  }

  if (subcommand == "remove" || subcommand == "uninstall") {
    if (args.size() < 2) {
      std::cerr << "Usage: qinglongclaw skills remove <name>\n";
      return 1;
    }
    std::string error;
    if (!manager.uninstall(args[1], &error)) {
      std::cerr << "Failed to remove skill: " << error << "\n";
      return 1;
    }
    std::cout << "Skill removed: " << args[1] << "\n";
    return 0;
  }

  if (subcommand == "install-builtin") {
    const auto builtin_dir = find_builtin_skills_dir();
    if (builtin_dir.empty()) {
      std::cerr << "No builtin skills directory found.\n";
      return 1;
    }
    std::string error;
    if (!manager.install_builtin(builtin_dir, &error)) {
      std::cerr << "Failed to install builtin skills: " << error << "\n";
      return 1;
    }
    std::cout << "Builtin skills installed from " << builtin_dir.string() << "\n";
    return 0;
  }

  if (subcommand == "list-builtin") {
    const auto builtin_dir = find_builtin_skills_dir();
    if (builtin_dir.empty()) {
      std::cout << "No builtin skills found.\n";
      return 0;
    }
    for (const auto& entry : std::filesystem::directory_iterator(builtin_dir)) {
      if (entry.is_directory()) {
        std::cout << "- " << entry.path().filename().string() << "\n";
      }
    }
    return 0;
  }

  if (subcommand == "search") {
    std::string error;
    const auto skills = manager.search_online(&error);
    if (!error.empty()) {
      std::cerr << "Failed to search skills: " << error << "\n";
      return 1;
    }
    for (const auto& skill : skills) {
      std::cout << "- " << skill.name << ": " << skill.description << "\n";
      if (!skill.repository.empty()) {
        std::cout << "  repo: " << skill.repository << "\n";
      }
    }
    return 0;
  }

  if (subcommand == "show") {
    if (args.size() < 2) {
      std::cerr << "Usage: qinglongclaw skills show <name>\n";
      return 1;
    }
    const auto content = manager.show(args[1]);
    if (!content.has_value()) {
      std::cerr << "Skill not found: " << args[1] << "\n";
      return 1;
    }
    std::cout << content.value() << "\n";
    return 0;
  }

  print_skills_help();
  return 1;
}

std::string format_schedule(const CronSchedule& schedule) {
  if (schedule.kind == "every" && schedule.every_ms.has_value()) {
    std::ostringstream out;
    out << "every " << (schedule.every_ms.value() / 1000) << "s";
    return out.str();
  }
  if (schedule.kind == "cron") {
    return schedule.expr;
  }
  if (schedule.kind == "at" && schedule.at_ms.has_value()) {
    const auto tp = std::chrono::system_clock::time_point(std::chrono::milliseconds(schedule.at_ms.value()));
    return format_local_time(tp);
  }
  return "unknown";
}

int cmd_cron(const std::vector<std::string>& args) {
  if (args.empty()) {
    print_cron_help();
    return 0;
  }

  Config config = load_config();
  const auto store_path = std::filesystem::path(config.workspace_path()) / "cron" / "jobs.json";
  CronService cron(store_path);

  const std::string subcommand = args[0];
  if (subcommand == "list") {
    const auto jobs = cron.list_jobs(true);
    if (jobs.empty()) {
      std::cout << "No scheduled jobs.\n";
      return 0;
    }
    for (const auto& job : jobs) {
      std::cout << job.name << " (" << job.id << ")\n";
      std::cout << "  schedule: " << format_schedule(job.schedule) << "\n";
      std::cout << "  status: " << (job.enabled ? "enabled" : "disabled") << "\n";
      if (job.state.next_run_at_ms.has_value()) {
        const auto tp =
            std::chrono::system_clock::time_point(std::chrono::milliseconds(job.state.next_run_at_ms.value()));
        std::cout << "  next run: " << format_local_time(tp) << "\n";
      }
    }
    return 0;
  }

  if (subcommand == "add") {
    const auto name_opt = find_option(args, "-n", "--name");
    const auto message_opt = find_option(args, "-m", "--message");
    const auto every_opt = find_option(args, "-e", "--every");
    const auto cron_opt = find_option(args, "-c", "--cron");
    const bool deliver = has_flag(args, "--deliver", "-d");
    const auto channel_opt = find_option(args, "--channel");
    const auto to_opt = find_option(args, "--to");

    if (!name_opt.has_value() || !message_opt.has_value()) {
      std::cerr << "Error: --name and --message are required\n";
      return 1;
    }
    if (!every_opt.has_value() && !cron_opt.has_value()) {
      std::cerr << "Error: either --every or --cron must be specified\n";
      return 1;
    }

    CronSchedule schedule;
    if (every_opt.has_value()) {
      const auto seconds = parse_int64(*every_opt);
      if (!seconds.has_value() || seconds.value() <= 0) {
        std::cerr << "Error: invalid --every value\n";
        return 1;
      }
      schedule.kind = "every";
      schedule.every_ms = seconds.value() * 1000;
    } else {
      schedule.kind = "cron";
      schedule.expr = *cron_opt;
    }

    const auto job = cron.add_job(*name_opt,
                                  schedule,
                                  *message_opt,
                                  deliver,
                                  channel_opt.value_or(""),
                                  to_opt.value_or(""));
    if (!job.has_value()) {
      std::cerr << "Failed to add job.\n";
      return 1;
    }
    std::cout << "Added job " << job->name << " (" << job->id << ")\n";
    return 0;
  }

  if (subcommand == "remove" && args.size() >= 2) {
    if (cron.remove_job(args[1])) {
      std::cout << "Removed job " << args[1] << "\n";
      return 0;
    }
    std::cerr << "Job not found: " << args[1] << "\n";
    return 1;
  }

  if ((subcommand == "enable" || subcommand == "disable") && args.size() >= 2) {
    const bool enable = subcommand == "enable";
    const auto job = cron.enable_job(args[1], enable);
    if (!job.has_value()) {
      std::cerr << "Job not found: " << args[1] << "\n";
      return 1;
    }
    std::cout << "Job " << job->name << " " << (enable ? "enabled" : "disabled") << "\n";
    return 0;
  }

  print_cron_help();
  return 1;
}

}  // namespace

int run_cli(const int argc, char** argv) {
  const auto args = args_to_vector(argc, argv);
  if (args.size() < 2) {
    print_help();
    return 1;
  }

  const std::string command = args[1];
  std::vector<std::string> tail;
  for (std::size_t i = 2; i < args.size(); ++i) {
    tail.push_back(args[i]);
  }

  if (command == "onboard") {
    return cmd_onboard();
  }
  if (command == "agent") {
    return cmd_agent(tail);
  }
  if (command == "gateway") {
    return cmd_gateway(tail);
  }
  if (command == "status") {
    return cmd_status();
  }
  if (command == "migrate") {
    return cmd_migrate();
  }
  if (command == "auth") {
    return cmd_auth(tail);
  }
  if (command == "cron") {
    return cmd_cron(tail);
  }
  if (command == "skills") {
    return cmd_skills(tail);
  }
  if (command == "version" || command == "--version" || command == "-v") {
    return cmd_version(tail);
  }

  std::cerr << "Unknown command: " << command << "\n";
  print_help();
  return 1;
}

}  // namespace QingLongClaw
