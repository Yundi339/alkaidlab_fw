#include "fw/LogConfig.hpp"
#include <hv/hlog.h>
#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>
#include <string>

namespace alkaidlab {
namespace fw {

static bool s_initialized = false;

void LogConfig::setFile(const char* filepath) {
    hlog_set_file(filepath);
}

void LogConfig::setMaxFileSize(size_t bytes) {
    hlog_set_max_filesize(bytes);
}

void LogConfig::setRemainDays(int days) {
    hlog_set_remain_days(days);
}

void LogConfig::setLevel(int level) {
    // Map fw::LogConfig::Level to libhv LOG_LEVEL_*
    int hvLevel;
    switch (level) {
    case kDebug: hvLevel = LOG_LEVEL_DEBUG; break;
    case kInfo:  hvLevel = LOG_LEVEL_INFO;  break;
    case kWarn:  hvLevel = LOG_LEVEL_WARN;  break;
    case kError: hvLevel = LOG_LEVEL_ERROR; break;
    default:     hvLevel = LOG_LEVEL_INFO;  break;
    }
    hlog_set_level(hvLevel);
}

bool LogConfig::ensureDirectory(const std::string& dir) {
    if (dir.empty()) return false;
    boost::system::error_code ec;
    boost::filesystem::path p(dir);
    if (boost::filesystem::exists(p, ec)) {
        return boost::filesystem::is_directory(p, ec);
    }
    boost::filesystem::create_directories(p, ec);
    return !ec && boost::filesystem::is_directory(p, ec);
}

bool LogConfig::initialize(const std::string& logDir,
                           size_t maxFileSize,
                           int remainDays) {
    if (s_initialized) return true;
    if (!ensureDirectory(logDir)) return false;
    std::string logFile = logDir + "/fw.log";
    setFile(logFile.c_str());
    setMaxFileSize(maxFileSize);
    setRemainDays(remainDays);
    setLevel(kInfo);
    s_initialized = true;
    return true;
}

} // namespace fw
} // namespace alkaidlab
