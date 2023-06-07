#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include "logging.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include <shared_mutex>

#define LOG_LINE_MAX_WIDTH 2048
#ifndef LOG_FILENAME
#define LOG_FILENAME "scrcpy_debug.log"
#endif //!LOG_FILENAME

class logger_config {
    private:
        int m_enabled = -1;
        std::shared_ptr<spdlog::logger> logger = NULL;
    public:
        logger_config() {
            init_debug_status();
        }
        void init_logger() {
            spdlog::set_pattern("[%H:%M:%S %z] [%s#%#-%!] [%n] [%^---%L---%$] [thread %t] %v");
            if (m_enabled >= 0) {
                spdlog::level::level_enum target_level = spdlog::level::debug;
                switch(m_enabled) {
                    case 0:
                        target_level = spdlog::level::trace;
                        break;
                    case 1:
                        target_level = spdlog::level::debug;
                        break;
                    case 2:
                        target_level = spdlog::level::info;
                        break;
                    case 3:
                        target_level = spdlog::level::warn;
                        break;
                    case 4:
                        target_level = spdlog::level::err;
                        break;
                    case 5:
                        target_level = spdlog::level::critical;
                        break;
                    case 6:
                        target_level = spdlog::level::off;
                        break;
                    default:
                        target_level = spdlog::level::debug;
                        break;
                }
                spdlog::set_level(target_level);
                spdlog::flush_every(std::chrono::milliseconds(200));
                try {
                    this->logger = spdlog::basic_logger_mt("default", LOG_FILENAME);
                    spdlog::set_default_logger(this->logger);
                    this->logger->flush_on(spdlog::level::debug);
                    this->logger->flush_on(spdlog::level::info);
                    this->logger->flush_on(spdlog::level::warn);
                    this->logger->flush_on(spdlog::level::err);
                    printf("Will logging into %s\n", LOG_FILENAME);
                } catch(const spdlog::spdlog_ex &ex) {
                    printf("Failed to create logger: %s\n", ex.what());
                }
            } else {
                spdlog::set_level(spdlog::level::info);
            }
        }
        void init_debug_status() {
            int env_value_len = 16;
            size_t read_len;
            char *env_value = (char*) malloc(env_value_len * sizeof(char));
            _dupenv_s(&env_value, &read_len, "SCRCPY_DEBUG");
            if (!env_value) {
                return;
            }
            printf("SCRCPY_DEBUG=%s\n", env_value);
            if (strlen(env_value) <= 0) {
                m_enabled = false;
            } else {
                int value = std::atoi(env_value);
                m_enabled = value;
            }
            printf("SCRCPY_DEBUG=%s, debug output level ? %d\n", env_value, m_enabled);
            free(env_value);
            init_logger();
        }
        void flush() {
            if(this->logger) {
                this->logger->flush();
            }
        }
};

logger_config *cfg = new logger_config();
void logging_cleanup() {
    if (!cfg) {
        return;
    }
    delete cfg;
    cfg = NULL;
}
void log_flush() {
    if (!cfg) {
        return;
    }
    cfg->flush();
}
