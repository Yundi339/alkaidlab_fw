#ifndef ALKAIDLAB_FW_LOGGER_HPP
#define ALKAIDLAB_FW_LOGGER_HPP

#include <string>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include "fw/WindowsCompat.hpp"  // spdlog 可能在 Windows 拉入 windows.h 污染 ERROR 宏

namespace alkaidlab {
namespace fw {

enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4
};

class Logger {
public:
    static Logger& getInstance();
    
    void setLevel(LogLevel level);
    LogLevel getLevel() const;
    
    void setLogFile(const std::string& logDir,
                   size_t maxFileSize = 100ULL * 1024 * 1024,
                   size_t maxFiles = 5);

    void setConsoleEnabled(bool enabled);

    void log(LogLevel level, const std::string& message);
    
    void trace(const std::string& message);
    void debug(const std::string& message);
    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);
    
    void tracef(const char* format, ...);
    void debugf(const char* format, ...);
    void infof(const char* format, ...);
    void warnf(const char* format, ...);
    void errorf(const char* format, ...);
    
    void flush();

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    void initializeLogger();
    spdlog::level::level_enum toSpdlogLevel(LogLevel level) const;
    LogLevel fromSpdlogLevel(spdlog::level::level_enum level) const;

private:
    std::shared_ptr<spdlog::logger> m_logger;
    LogLevel m_level;
    std::string m_logFile;
    bool m_consoleEnabled;
    size_t m_maxFileSize;
    size_t m_maxFiles;

    void recreateLogger();
};

} // namespace fw

// 全局日志宏
#undef LOG_TRACE
#undef LOG_DEBUG
#undef LOG_INFO
#undef LOG_WARN
#undef LOG_ERROR
#define LOG_TRACE(msg) alkaidlab::fw::Logger::getInstance().trace(msg)
#define LOG_DEBUG(msg) alkaidlab::fw::Logger::getInstance().debug(msg)
#define LOG_INFO(msg) alkaidlab::fw::Logger::getInstance().info(msg)
#define LOG_WARN(msg) alkaidlab::fw::Logger::getInstance().warn(msg)
#define LOG_ERROR(msg) alkaidlab::fw::Logger::getInstance().error(msg)

using Logger = fw::Logger;
using LogLevel = fw::LogLevel;

} // namespace alkaidlab

#endif // ALKAIDLAB_FW_LOGGER_HPP
