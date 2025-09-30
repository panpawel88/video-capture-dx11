#include "Logger.h"

Logger& Logger::GetInstance() {
    static Logger instance;
    return instance;
}

void Logger::SetLogLevel(LogLevel level) {
    m_logLevel = level;
}

LogLevel Logger::GetLogLevel() const {
    return m_logLevel;
}

void Logger::Error(const std::string& message) {
    if (m_logLevel >= LogLevel::Error) {
        std::cout << "[ERROR] " << message << "\n";
    }
}

void Logger::Warning(const std::string& message) {
    if (m_logLevel >= LogLevel::Warning) {
        std::cout << "[WARNING] " << message << "\n";
    }
}

void Logger::Info(const std::string& message) {
    if (m_logLevel >= LogLevel::Info) {
        std::cout << "[INFO] " << message << "\n";
    }
}

void Logger::Debug(const std::string& message) {
    if (m_logLevel >= LogLevel::Debug) {
        std::cout << "[DEBUG] " << message << "\n";
    }
}