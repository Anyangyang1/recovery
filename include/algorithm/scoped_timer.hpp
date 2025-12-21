#pragma once
#include "ylt/easylog.hpp"
#include <chrono>
#include <string>

namespace ECProject {

class ScopedTimer {
public:
    explicit ScopedTimer(
        std::string msg,
        const char* file,
        int line,
        easylog::Severity level = easylog::Severity::WARNING
    ) : msg_(std::move(msg)), file_(file), line_(line), level_(level),
        start_(std::chrono::high_resolution_clock::now()) {}

    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration<double, std::milli>(end - start_).count();

        // 构造带位置的消息
        std::string full_msg = "[XXX.cpp:" + std::to_string(line_) + "] " + msg_;

        switch (level_) {
        case easylog::Severity::DEBUG:
            ELOG(DEBUG) << full_msg << " [time]: " << ms << " ms.";
            break;
        case easylog::Severity::INFO:
            ELOG(INFO) << full_msg << " [time]: " << ms << " ms.";
            break;
        case easylog::Severity::WARNING:
            ELOG(WARNING) << full_msg << " [time]: " << ms << " ms.";
            break;
        case easylog::Severity::ERROR:
            ELOG(ERROR) << full_msg << " [time]: " << ms << " ms.";
            break;
        default:
            ELOG(WARNING) << full_msg << " [time]: " << ms << " ms.";
        }
    }

private:
    std::string msg_;
    const char* file_;
    int line_;
    easylog::Severity level_;
    std::chrono::high_resolution_clock::time_point start_;
};

// 宏显式传入 __FILE__, __LINE__
#define SCOPED_TIMER(msg) \
    ::ECProject::ScopedTimer _timer { (msg), __FILE__, __LINE__ }

} // namespace ECProject