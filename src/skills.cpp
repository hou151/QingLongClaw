#include "QingLongClaw/skills.h"

#include <sstream>

#include "json.hpp"
#include "QingLongClaw/http_client.h"
#include "QingLongClaw/util.h"

namespace QingLongClaw {

SkillManager::SkillManager(std::filesystem::path workspace) : workspace_(std::move(workspace)) {}

std::filesystem::path SkillManager::skills_dir() const { return workspace_ / "skills"; }

std::string SkillManager::extract_description(const std::string& skill_markdown) {
  for (const auto& line_raw : split_lines(skill_markdown)) {
    const std::string line = trim(line_raw);
    if (line.empty()) {
      continue;
    }
    if (line.rfind("#", 0) == 0) {
      continue;
    }
    if (line.rfind("description:", 0) == 0) {
      return trim(line.substr(std::string("description:").size()));
    }
    return line;
  }
  return "";
}

std::vector<SkillInfo> SkillManager::list_installed() const {
  std::vector<SkillInfo> skills;
  std::error_code ec;
  const auto dir = skills_dir();
  if (!std::filesystem::exists(dir, ec)) {
    return skills;
  }

  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (ec || !entry.is_directory()) {
      continue;
    }
    const auto skill_file = entry.path() / "SKILL.md";
    const auto content = read_text_file(skill_file);

    SkillInfo info;
    info.name = entry.path().filename().string();
    info.source = "workspace";
    info.description = extract_description(content);
    skills.push_back(std::move(info));
  }
  return skills;
}

std::optional<std::string> SkillManager::show(const std::string& skill_name) const {
  const auto content = read_text_file(skills_dir() / skill_name / "SKILL.md");
  if (content.empty()) {
    return std::nullopt;
  }
  return content;
}

bool SkillManager::install_from_github(const std::string& repo, std::string* error_message) {
  const auto repo_name = std::filesystem::path(repo).filename().string();
  if (repo_name.empty()) {
    if (error_message != nullptr) {
      *error_message = "invalid repo";
    }
    return false;
  }

  const auto target_dir = skills_dir() / repo_name;
  std::error_code ec;
  if (std::filesystem::exists(target_dir, ec)) {
    if (error_message != nullptr) {
      *error_message = "skill already installed: " + repo_name;
    }
    return false;
  }

  const std::string url = "https://raw.githubusercontent.com/" + repo + "/main/SKILL.md";
  const HttpResponse response = HttpClient::get(url, {}, "", 20);
  if (!response.error.empty()) {
    if (error_message != nullptr) {
      *error_message = response.error;
    }
    return false;
  }
  if (response.status != 200) {
    if (error_message != nullptr) {
      std::ostringstream out;
      out << "http status " << response.status;
      *error_message = out.str();
    }
    return false;
  }
  if (!write_text_file(target_dir / "SKILL.md", response.body)) {
    if (error_message != nullptr) {
      *error_message = "failed to write SKILL.md";
    }
    return false;
  }
  return true;
}

bool SkillManager::uninstall(const std::string& skill_name, std::string* error_message) {
  const auto target_dir = skills_dir() / skill_name;
  std::error_code ec;
  if (!std::filesystem::exists(target_dir, ec)) {
    if (error_message != nullptr) {
      *error_message = "skill not found: " + skill_name;
    }
    return false;
  }
  std::filesystem::remove_all(target_dir, ec);
  if (ec) {
    if (error_message != nullptr) {
      *error_message = "failed to remove skill: " + ec.message();
    }
    return false;
  }
  return true;
}

bool SkillManager::install_builtin(const std::filesystem::path& builtin_skills_dir, std::string* error_message) {
  std::error_code ec;
  if (!std::filesystem::exists(builtin_skills_dir, ec)) {
    if (error_message != nullptr) {
      *error_message = "builtin skills directory not found: " + builtin_skills_dir.string();
    }
    return false;
  }

  bool copied_any = false;
  for (const auto& entry : std::filesystem::directory_iterator(builtin_skills_dir, ec)) {
    if (ec || !entry.is_directory()) {
      continue;
    }
    const auto target = skills_dir() / entry.path().filename();
    std::filesystem::remove_all(target, ec);
    ec.clear();
    if (copy_directory_recursive(entry.path(), target)) {
      copied_any = true;
    }
  }

  if (!copied_any && error_message != nullptr) {
    *error_message = "no builtin skills copied";
  }
  return copied_any;
}

std::vector<SkillInfo> SkillManager::search_online(std::string* error_message) const {
  std::vector<SkillInfo> skills;
  const HttpResponse response = HttpClient::get(
      "https://raw.githubusercontent.com/sipeed/picoclaw-skills/main/skills.json", {}, "", 20);
  if (!response.error.empty()) {
    if (error_message != nullptr) {
      *error_message = response.error;
    }
    return skills;
  }
  if (response.status != 200) {
    if (error_message != nullptr) {
      std::ostringstream out;
      out << "http status " << response.status;
      *error_message = out.str();
    }
    return skills;
  }

  try {
    const auto payload = nlohmann::json::parse(response.body);
    if (!payload.is_array()) {
      if (error_message != nullptr) {
        *error_message = "unexpected response payload";
      }
      return skills;
    }
    for (const auto& item : payload) {
      if (!item.is_object()) {
        continue;
      }
      SkillInfo info;
      info.name = item.value("name", "");
      info.repository = item.value("repository", "");
      info.description = item.value("description", "");
      info.author = item.value("author", "");
      if (item.contains("tags") && item["tags"].is_array()) {
        for (const auto& tag : item["tags"]) {
          if (tag.is_string()) {
            info.tags.push_back(tag.get<std::string>());
          }
        }
      }
      if (!info.name.empty()) {
        skills.push_back(std::move(info));
      }
    }
  } catch (const std::exception& ex) {
    if (error_message != nullptr) {
      *error_message = ex.what();
    }
  }

  return skills;
}

}  // namespace QingLongClaw

