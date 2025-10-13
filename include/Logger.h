#pragma once

#include <iostream>
#include <string>
#include <sstream>

enum class LogLevel {
    Error = 0,
    Warning = 1,
    Info = 2,
    Debug = 3
};

class Logger {
public:
    static Logger& GetInstance();

    void SetLogLevel(LogLevel level);
    LogLevel GetLogLevel() const;

    void Error(const std::string& message);
    void Warning(const std::string& message);
    void Info(const std::string& message);
    void Debug(const std::string& message);

    template<typename... Args>
    void Error(const Args&... args) {
        if (m_logLevel >= LogLevel::Error) {
            LogMessage("[ERROR] ", args...);
        }
    }

    template<typename... Args>
    void Warning(const Args&... args) {
        if (m_logLevel >= LogLevel::Warning) {
            LogMessage("[WARNING] ", args...);
        }
    }

    template<typename... Args>
    void Info(const Args&... args) {
        if (m_logLevel >= LogLevel::Info) {
            LogMessage("[INFO] ", args...);
        }
    }

    template<typename... Args>
    void Debug(const Args&... args) {
        if (m_logLevel >= LogLevel::Debug) {
            LogMessage("[DEBUG] ", args...);
        }
    }

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    template<typename... Args>
    void LogMessage(const std::string& prefix, const Args&... args) {
        std::ostringstream oss;
        oss << prefix;
        (oss << ... << args);
        oss << "\n";
        std::cout << oss.str();
    }

    LogLevel m_logLevel = LogLevel::Info;
};

#define LOG_ERROR(...) Logger::GetInstance().Error(__VA_ARGS__)
#define LOG_WARNING(...) Logger::GetInstance().Warning(__VA_ARGS__)
#define LOG_INFO(...) Logger::GetInstance().Info(__VA_ARGS__)
#define LOG_DEBUG(...) Logger::GetInstance().Debug(__VA_ARGS__)