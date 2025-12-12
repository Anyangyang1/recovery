// utils/scoped_timer.hpp
#pragma once
#include "ylt/easylog.hpp"
#include "ylt/easylog/record.hpp"
#include <chrono>
#include <string>
namespace ECProject {
class ScopedTimer {
  public:
    explicit ScopedTimer(std::string msg,
                         easylog::Severity level = easylog::Severity::DEBUG)
        : msg_(std::move(msg)), level_(level),
          start_(std::chrono::high_resolution_clock::now()) {}

    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto ms =
            std::chrono::duration<double, std::milli>(end - start_).count();
        // 根据 level 输出日志（避免 DEBUG 在 release 被 strip）
        if (level_ == easylog::Severity::DEBUG) {
            ELOG(DEBUG) << msg_ << " [time]: " << ms << " ms.";
        } else if (level_ == easylog::Severity::INFO) {
            ELOG(INFO) << msg_ << " [time]: " << ms << " ms.";
        }
    }

  private:
    std::string msg_;
    easylog::Severity level_;
    std::chrono::high_resolution_clock::time_point start_;
};

#define SCOPED_TIMER(msg)                                                      \
    ScopedTimer _timer { msg }
} // namespace ECProject