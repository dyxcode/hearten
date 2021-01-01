#ifndef HEARTEN_LOG_H_
#define HEARTEN_LOG_H_

#include <ctime>
#include <iomanip>
#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include <fstream>
#include <iostream>
#include <thread>

namespace hearten {

namespace detail {

class Noncopyable {
public:
  Noncopyable(const Noncopyable&) = delete;
  void operator=(const Noncopyable&) = delete;

protected:
  Noncopyable() = default;
  ~Noncopyable() = default;
  Noncopyable(Noncopyable&&) = default;
  Noncopyable& operator=(Noncopyable&&) = default;
};
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

  std::string toString() const {
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
class LogEvent : Noncopyable {
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

// 格式解析：
// %d{%Y-%m-%d %H:%M:%S} : %d标识时间，后跟具体时间格式
// %t : Tab[\t]
// %n : 换行符[\r\n]
// %T : 线程ID
// %f : 文件名
// %l : 行号
// %c : 事件内容
// %p : 事件级别

// 日志器
class Logger : Noncopyable {
  using FormatFunctor = std::function<void(std::ostream&, const LogEvent&)>;
public:
  explicit Logger(const std::string& pattern =
      "[%p]:%c%t%f:%l%tTID:%T%t%d{%Y-%m-%d %H:%M:%S}%n") {
    size_t i = 0;
    std::string normal;

    while(i < pattern.size()) {
      if (pattern[i] != '%')
        normal.push_back(pattern[i++]);
      else {
        size_t j = i + 1;
        if (j == pattern.size()) {
          normal.push_back('%');
          break;
        }
        if (pattern[j] == '%') {
          normal.push_back('%');
          i = j + 1;
          continue;
        }
        if (pattern[j] == 'd') {
          if (++j == pattern.size()) {
            normal.append(pattern.substr(i));
            break;
          }
          if (pattern[j] == '{') {
            std::string time_pattern;
            while (++j < pattern.size() && pattern[j] != '}')
              time_pattern.push_back(pattern[j]);
            if (j == pattern.size()) {
              normal.append(pattern.substr(i));
              break;
            }

            if (!normal.empty()) {
              functors_.push_back([s = std::move(normal)]
                (std::ostream& os, const LogEvent& ev){ os << std::move(s); });
              normal.clear();
            }
            functors_.push_back([s = std::move(time_pattern)]
              (std::ostream& os, const LogEvent& ev){
                auto time = ev.getTime();
                auto p = std::localtime(&time);
                if (p) os << std::put_time(p, s.c_str());
                else os << "unknown time";
              });
            i = j + 1;
          }
          else {
            normal.append("%d");
            i = j;
          }
        }
        else {
          auto functor = getFunctor(pattern[j]);
          if (functor) {
            if (!normal.empty()) {
              functors_.push_back([s = std::move(normal)]
                (std::ostream& os, const LogEvent& ev){ os << std::move(s); });
              normal.clear();
            }
            functors_.push_back(std::move(functor));
          }
          else
            normal.append(pattern.substr(i, 2));
          i = j + 1;
        }
      }
    }
    if (!normal.empty()) {
      functors_.push_back([s = std::move(normal)]
        (std::ostream& os, const LogEvent& ev){ os << std::move(s); });
    }
  }

  void log(const LogEvent& event, std::ostream& os) const {
    os << getMessage(event);
  }

private:
  std::string getMessage(const LogEvent& ev) const {
    std::stringstream ss;
    for (auto && f : functors_) { f(ss, ev); }
    return ss.str();
  }
  FormatFunctor getFunctor(char ch) {
    if (ch == 't') return [](std::ostream& os, const LogEvent& ev)
      { os << "\t"; };
    if (ch == 'n') return [](std::ostream& os, const LogEvent& ev)
      { os << "\n"; };
    if (ch == 'T') return [](std::ostream& os, const LogEvent& ev)
      { os << ev.getThreadID(); };
    if (ch == 'f') return [](std::ostream& os, const LogEvent& ev)
      { os << ev.getFile(); };
    if (ch == 'l') return [](std::ostream& os, const LogEvent& ev)
      { os << ev.getLine(); };
    if (ch == 'c') return [](std::ostream& os, const LogEvent& ev)
      { os << ev.getContent(); };
    if (ch == 'p') return [](std::ostream& os, const LogEvent& ev)
      { os << ev.getLevel().toString(); };
    return nullptr;
  }

  std::vector<FormatFunctor> functors_;
};

} // namespace detail

class LogStream : detail::Noncopyable {
public:
  explicit LogStream(const char* file, uint32_t line, detail::LogLevel level)
    : file_(file), line_(line), level_(level) { }

  ~LogStream() {
    detail::LogEvent event{file_, line_, level_, ss_.str()};
    logger_.log(event, std::cout);
  }

  template<typename T> LogStream& operator<<(T obj) {
    ss_ << std::move(obj);
    return *this;
  }

private:
  const char* file_;        // 文件名
  uint32_t line_;           // 行号
  detail::LogLevel level_;  // 事件级别
  detail::Logger logger_;
  std::stringstream ss_;
};

} // namespace hearten

#define DEBUG hearten::LogStream(__FILE__, __LINE__,\
    hearten::detail::LogLevel::kDebug)
#define INFO hearten::LogStream(__FILE__, __LINE__,\
    hearten::detail::LogLevel::kInfo)
#define WARN hearten::LogStream(__FILE__, __LINE__,\
    hearten::detail::LogLevel::kWarn)
#define ERROR hearten::LogStream(__FILE__, __LINE__,\
    hearten::detail::LogLevel::kError)
#define FATAL hearten::LogStream(__FILE__, __LINE__,\
    hearten::detail::LogLevel::kFatal)

#define ASSERT(authenticity) do { \
  if (!(authenticity)) { \
    FATAL << "assert trigger"; \
    exit(1); \
  } \
} while (0)

#endif
