#ifndef HEARTEN_LOG_LOG_H_
#define HEARTEN_LOG_LOG_H_

#include "log/logbase.h"
#include "log/logger.h"
#include "log/logstream.h"

#define DEBUG hearten::LogStream(__FILE__, __LINE__, hearten::LogLevel::kDebug)
#define INFO hearten::LogStream(__FILE__, __LINE__, hearten::LogLevel::kInfo)
#define WARN hearten::LogStream(__FILE__, __LINE__, hearten::LogLevel::kWarn)
#define ERROR hearten::LogStream(__FILE__, __LINE__, hearten::LogLevel::kError)
#define FATAL hearten::LogStream(__FILE__, __LINE__, hearten::LogLevel::kFatal)

#define ASSERT(authenticity) do { \
  if (!(authenticity)) { \
    FATAL << "assert trigger"; \
    exit(1); \
  } \
} while (0)

#endif
