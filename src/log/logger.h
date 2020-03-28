#ifndef HEARTEN_LOG_LOGGER_H_
#define HEARTEN_LOG_LOGGER_H_

#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>
#include <functional>

#include "log/logbase.h"

namespace hearten {

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
class Logger : public Noncopyable {
  using FormatFunctor = std::function<void(std::ostream&, CR<LogEvent>)>;
public:
  explicit Logger(const std::string& pattern = "[%p]:%c%t%f:%l%tTID:%T%t%d{%Y-%m-%d %H:%M:%S}%n") {
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
                (std::ostream& os, CR<LogEvent> ev){ os << std::move(s); });
              normal.clear();
            }
            functors_.push_back([s = std::move(time_pattern)]
              (std::ostream& os, CR<LogEvent> ev){
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
                (std::ostream& os, CR<LogEvent> ev){ os << std::move(s); });
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
        (std::ostream& os, CR<LogEvent> ev){ os << std::move(s); });
    }
  }

  template<typename Iter>
  void log(CR<LogEvent> event, Iter beg, Iter end) const {
    std::string msg = getMessage(event);
    for (;beg != end; std::advance(beg, 1)) { beg->log(msg); }
  }
  void log(CR<LogEvent> event, CR<LogAppender> appender) const {
    std::string msg = getMessage(event);
    appender.log(msg);
  }

private:
  std::string getMessage(CR<LogEvent> ev) const {
    std::stringstream ss;
    for (auto && f : functors_) { f(ss, ev); }
    return ss.str();
  }
  FormatFunctor getFunctor(char ch) {
    if (ch == 't') return [](std::ostream& os, CR<LogEvent> ev)
      { os << "\t"; };
    if (ch == 'n') return [](std::ostream& os, CR<LogEvent> ev)
      { os << "\n"; };
    if (ch == 'T') return [](std::ostream& os, CR<LogEvent> ev)
      { os << ev.getThreadID(); };
    if (ch == 'f') return [](std::ostream& os, CR<LogEvent> ev)
      { os << ev.getFile(); };
    if (ch == 'l') return [](std::ostream& os, CR<LogEvent> ev)
      { os << ev.getLine(); };
    if (ch == 'c') return [](std::ostream& os, CR<LogEvent> ev)
      { os << ev.getContent(); };
    if (ch == 'p') return [](std::ostream& os, CR<LogEvent> ev)
      { os << ev.getLevel().toString(); };
    return nullptr;
  }

  std::vector<FormatFunctor> functors_;
};

} // namespace hearten

#endif
