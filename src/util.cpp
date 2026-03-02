#include "QingLongClaw/util.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace QingLongClaw {

std::string home_dir() {
#ifdef _WIN32
  const char* user_profile = std::getenv("USERPROFILE");
  if (user_profile != nullptr && user_profile[0] != '\0') {
    return std::string(user_profile);
  }
  const char* home_drive = std::getenv("HOMEDRIVE");
  const char* home_path = std::getenv("HOMEPATH");
  if (home_drive != nullptr && home_path != nullptr) {
    return std::string(home_drive) + std::string(home_path);
  }
  return "C:\\";
#else
  const char* home = std::getenv("HOME");
  if (home != nullptr && home[0] != '\0') {
    return std::string(home);
  }
  return "/";
#endif
}

std::filesystem::path config_root() { return std::filesystem::path(home_dir()) / ".qinglongclaw"; }

std::string expand_home(const std::string& path) {
  if (path.empty()) {
    return path;
  }
  if (path[0] == '~') {
    if (path.size() == 1) {
      return home_dir();
    }
    if (path[1] == '/' || path[1] == '\\') {
      return home_dir() + path.substr(1);
    }
  }
  return path;
}

std::string trim(std::string value) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

std::string to_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string replace_all(std::string value, const std::string& from, const std::string& to) {
  if (from.empty()) {
    return value;
  }
  std::size_t start = 0;
  while ((start = value.find(from, start)) != std::string::npos) {
    value.replace(start, from.size(), to);
    start += to.size();
  }
  return value;
}

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return "";
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

bool write_text_file(const std::filesystem::path& path, const std::string& data) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }
  output << data;
  return static_cast<bool>(output);
}

bool copy_directory_recursive(const std::filesystem::path& src, const std::filesystem::path& dst) {
  std::error_code ec;
  if (!std::filesystem::exists(src, ec)) {
    return false;
  }
  std::filesystem::create_directories(dst, ec);
  if (ec) {
    return false;
  }

  for (const auto& entry : std::filesystem::recursive_directory_iterator(src, ec)) {
    if (ec) {
      return false;
    }
    const auto relative = std::filesystem::relative(entry.path(), src, ec);
    if (ec) {
      return false;
    }
    const auto target = dst / relative;
    if (entry.is_directory()) {
      std::filesystem::create_directories(target, ec);
      if (ec) {
        return false;
      }
      continue;
    }
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) {
      return false;
    }
    std::filesystem::copy_file(entry.path(), target, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
      return false;
    }
  }
  return true;
}

std::string sanitize_filename(const std::string& value) {
  std::string output;
  output.reserve(value.size());
  for (char c : value) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') {
      output.push_back(c);
    } else {
      output.push_back('_');
    }
  }
  if (output.empty()) {
    return "default";
  }
  return output;
}

std::string sanitize_utf8_lossy(const std::string& value) {
  std::string out;
  out.reserve(value.size());

  const auto append_replacement = [&out]() { out.append("\xEF\xBF\xBD"); };

  std::size_t i = 0;
  while (i < value.size()) {
    const auto b0 = static_cast<unsigned char>(value[i]);
    if (b0 <= 0x7F) {
      out.push_back(static_cast<char>(b0));
      ++i;
      continue;
    }

    std::size_t width = 0;
    if (b0 >= 0xC2 && b0 <= 0xDF) {
      width = 2;
    } else if (b0 >= 0xE0 && b0 <= 0xEF) {
      width = 3;
    } else if (b0 >= 0xF0 && b0 <= 0xF4) {
      width = 4;
    } else {
      append_replacement();
      ++i;
      continue;
    }

    if (i + width > value.size()) {
      append_replacement();
      break;
    }

    const auto b1 = static_cast<unsigned char>(value[i + 1]);
    const auto is_cont = [](unsigned char byte) { return byte >= 0x80 && byte <= 0xBF; };
    bool valid = is_cont(b1);
    if (!valid) {
      append_replacement();
      ++i;
      continue;
    }

    if (width == 3) {
      const auto b2 = static_cast<unsigned char>(value[i + 2]);
      valid = is_cont(b2);
      if (valid && b0 == 0xE0) {
        valid = (b1 >= 0xA0 && b1 <= 0xBF);
      } else if (valid && b0 == 0xED) {
        valid = (b1 >= 0x80 && b1 <= 0x9F);
      }
    } else if (width == 4) {
      const auto b2 = static_cast<unsigned char>(value[i + 2]);
      const auto b3 = static_cast<unsigned char>(value[i + 3]);
      valid = is_cont(b2) && is_cont(b3);
      if (valid && b0 == 0xF0) {
        valid = (b1 >= 0x90 && b1 <= 0xBF);
      } else if (valid && b0 == 0xF4) {
        valid = (b1 >= 0x80 && b1 <= 0x8F);
      }
    }

    if (!valid) {
      append_replacement();
      ++i;
      continue;
    }

    out.append(value, i, width);
    i += width;
  }

  return out;
}

std::int64_t unix_ms_now() {
  const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
  return now.time_since_epoch().count();
}

std::string format_local_time(std::chrono::system_clock::time_point tp) {
  const std::time_t t = std::chrono::system_clock::to_time_t(tp);
  std::tm local_tm{};
#ifdef _WIN32
  localtime_s(&local_tm, &t);
#else
  localtime_r(&t, &local_tm);
#endif
  std::ostringstream out;
  out << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
  return out.str();
}

std::optional<std::int64_t> parse_int64(const std::string& value) {
  try {
    std::size_t idx = 0;
    const auto parsed = std::stoll(value, &idx, 10);
    if (idx != value.size()) {
      return std::nullopt;
    }
    return parsed;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<double> parse_double(const std::string& value) {
  try {
    std::size_t idx = 0;
    const auto parsed = std::stod(value, &idx);
    if (idx != value.size()) {
      return std::nullopt;
    }
    return parsed;
  } catch (...) {
    return std::nullopt;
  }
}

std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    lines.push_back(line);
  }
  return lines;
}

}  // namespace QingLongClaw
