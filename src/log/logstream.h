#ifndef HEARTEN_LOG_LOGSTREAM_H_
#define HEARTEN_LOG_LOGSTREAM_H_

#include <iostream>

#include "log/logger.h"

namespace hearten {

class LogStream : public Noncopyable {
public:
  explicit LogStream(const char* file, uint32_t line, LogLevel level) :
    file_(file), line_(line), level_(level) { }

  ~LogStream() {
    LogEvent event{file_, line_, level_, ss_.str()};
    logger_.log(event, LogAppender(std::cout));
  }

  template<typename T> LogStream& operator<<(const T& obj) {
    ss_ << obj;
    return *this;
  }

private:
  const char* file_;// 文件名
  uint32_t line_;   // 行号
  LogLevel level_;  // 日志级别
  Logger logger_;
  std::stringstream ss_;
};

} // namespace hearten

#endif
