#pragma once

#include <atomic>
#include <cstdio>
#include <format>
#include <print>

#include "coverbs_rpc/log.hpp"

namespace coverbs_rpc::detail {

class logger {
public:
  void set_level(log_level level) { level_.store(level, std::memory_order_relaxed); }

  template <typename... Args>
  void trace(std::format_string<Args...> fmt, Args &&...args) {
    log(log_level::trace, "TRACE", fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void debug(std::format_string<Args...> fmt, Args &&...args) {
    log(log_level::debug, "DEBUG", fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void info(std::format_string<Args...> fmt, Args &&...args) {
    log(log_level::info, "INFO ", fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void warn(std::format_string<Args...> fmt, Args &&...args) {
    log(log_level::warn, "WARN ", fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void error(std::format_string<Args...> fmt, Args &&...args) {
    log(log_level::err, "ERROR", fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void critical(std::format_string<Args...> fmt, Args &&...args) {
    log(log_level::critical, "CRIT ", fmt, std::forward<Args>(args)...);
  }

private:
  template <typename... Args>
  void log(log_level level, const char *level_str, std::format_string<Args...> fmt,
           Args &&...args) {
    if (level >= level_.load(std::memory_order_relaxed)) {
      std::println(stderr, "[coverbs_rpc] [{}] {}", level_str,
                   std::format(fmt, std::forward<Args>(args)...));
    }
  }

  std::atomic<log_level> level_{log_level::info};
};

inline logger *get_logger() {
  static logger instance;
  return &instance;
}

} // namespace coverbs_rpc::detail
