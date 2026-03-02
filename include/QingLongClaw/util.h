#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace QingLongClaw {

std::string home_dir();
std::filesystem::path config_root();
std::string expand_home(const std::string& path);
std::string trim(std::string value);
std::string to_lower(std::string value);
std::string replace_all(std::string value, const std::string& from, const std::string& to);
std::string read_text_file(const std::filesystem::path& path);
bool write_text_file(const std::filesystem::path& path, const std::string& data);
bool copy_directory_recursive(const std::filesystem::path& src, const std::filesystem::path& dst);
std::string sanitize_filename(const std::string& value);
std::string sanitize_utf8_lossy(const std::string& value);
std::int64_t unix_ms_now();
std::string format_local_time(std::chrono::system_clock::time_point tp);
std::optional<std::int64_t> parse_int64(const std::string& value);
std::optional<double> parse_double(const std::string& value);
std::vector<std::string> split_lines(const std::string& text);

}  // namespace QingLongClaw
