#ifndef LOG_H
#define LOG_H

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

inline void init_logging(const std::string& log_file = "btclient.log") {
    auto file_sink   = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, true);
    auto stdout_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

    file_sink->set_level(spdlog::level::debug);
    stdout_sink->set_level(spdlog::level::info);

    auto logger = std::make_shared<spdlog::logger>("btclient", spdlog::sinks_init_list{file_sink, stdout_sink});
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);
    spdlog::flush_on(spdlog::level::err);
}

#define LOG_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define LOG_INFO(...)  SPDLOG_INFO(__VA_ARGS__)
#define LOG_WARN(...)  SPDLOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
#define LOG_CRIT(...)  SPDLOG_CRITICAL(__VA_ARGS__)

#endif
