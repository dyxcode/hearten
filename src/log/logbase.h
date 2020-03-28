#ifndef HEARTEN_LOG_LOGBASE_H_
#define HEARTEN_LOG_LOGBASE_H_

#include <fstream>
#include <string>
#include <thread>

#include "util/util.h"

namespace hearten {

// 日志等级
class LogLevel {
public:
  enum Level { kDebug, kInfo, kWarn, kError, kFatal };

  LogLevel(LogLevel::Level level) : level_(level) { }

  bool operator<(LogLevel rhs) { return level_ < rhs.level_; }
  bool operator<=(LogLevel rhs) { return level_ <= rhs.level_; }
  bool operator==(LogLevel rhs) { return level_ == rhs.level_; }
  bool operator>(LogLevel rhs) { return level_ > rhs.level_; }
  bool operator>=(LogLevel rhs) { return level_ >= rhs.level_; }

  const char* toString() const {
    if (level_ == kDebug) return "DEBUG";
    if (level_ == kInfo)  return "INFO";
    if (level_ == kWarn)  return "WARN";
    if (level_ == kError) return "ERROR";
    if (level_ == kFatal) return "FATAL";
    return "UNKNOWN";
  }

private:
  Level level_;
};

// 日志事件
class LogEvent : public Noncopyable {
public:
  LogEvent(const char* file, uint32_t line, LogLevel level, std::string content,
            std::thread::id TID = std::this_thread::get_id()) :
    file_(file), line_(line), level_(level),
    content_(std::move(content)), threadID_(TID) {
    using SysClock = std::chrono::system_clock;
    time_ = SysClock::to_time_t(SysClock::now());
  }

  std::string getFile() const {
    std::string f(file_); return f.substr(f.find_last_of("/\\") + 1);
  }
  uint32_t getLine() const { return line_; }
  LogLevel getLevel() const { return level_; }
  std::string getContent() const { return content_; }
  std::thread::id getThreadID() const { return threadID_; }
  std::time_t getTime() const { return time_; }

private:
  const char* file_;        // 文件名
  uint32_t line_;           // 行号
  LogLevel level_;          // 日志等级
  std::string content_;     // 日志内容
  std::thread::id threadID_;// 线程ID
  std::time_t time_;        // 时间戳
};

// 日志输出地
class LogAppender : public Noncopyable {
public:
  explicit LogAppender(CR<std::ostream> os) : os_(os.rdbuf()) { }
  explicit LogAppender(CR<std::string> file) : fs_(file), os_(fs_.rdbuf()) { }

  void log(CR<std::string> message) const { os_ << message; }

private:
  std::ofstream fs_;
  mutable std::ostream os_;
};

} // namespace hearten

#endif
