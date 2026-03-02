#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace QingLongClaw {

struct SkillInfo {
  std::string name;
  std::string source;
  std::string description;
  std::string repository;
  std::string author;
  std::vector<std::string> tags;
};

class SkillManager {
 public:
  explicit SkillManager(std::filesystem::path workspace);

  std::vector<SkillInfo> list_installed() const;
  std::optional<std::string> show(const std::string& skill_name) const;

  bool install_from_github(const std::string& repo, std::string* error_message);
  bool uninstall(const std::string& skill_name, std::string* error_message);
  bool install_builtin(const std::filesystem::path& builtin_skills_dir, std::string* error_message);

  std::vector<SkillInfo> search_online(std::string* error_message) const;

 private:
  std::filesystem::path skills_dir() const;
  static std::string extract_description(const std::string& skill_markdown);

  std::filesystem::path workspace_;
};

}  // namespace QingLongClaw

