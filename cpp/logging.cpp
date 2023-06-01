#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include "logging.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include <shared_mutex>

#define LOG_LINE_MAX_WIDTH 2048

class logger_config {
    private:
        int m_enabled = 0;
        std::shared_ptr<spdlog::logger> logger = NULL;
    public:
        logger_config() {
            init_debug_status();
        }
        bool enabled() {
            return m_enabled;
        }
        void init_logger() {
            spdlog::set_pattern("[%H:%M:%S %z] [%s#%#%!] [%n] [%^---%L---%$] [thread %t] %v");
            if (m_enabled) {
                spdlog::set_level(m_enabled == 2 ? spdlog::level::trace : spdlog::level::debug);
                spdlog::flush_every(std::chrono::milliseconds(200));
                try {
                    this->logger = spdlog::basic_logger_mt("default", "scrcpy_debug.log");
                    spdlog::set_default_logger(this->logger);
                    this->logger->flush_on(spdlog::level::debug);
                    this->logger->flush_on(spdlog::level::info);
                    this->logger->flush_on(spdlog::level::warn);
                    this->logger->flush_on(spdlog::level::err);
                    printf("Will logging into scrcpy_debug.log\n");
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
            printf("SCRCPY_DEBUG=%s\n", env_value);
            if (env_value && strlen(env_value) <= 0) {
                m_enabled = false;
            } else {
                if  (strcmp(env_value, "2") == 0) {
                    m_enabled = 2;
                } else {
                    m_enabled = 1;
                }
            }
            printf("SCRCPY_DEBUG=%s, debug output enabled? %s\n", env_value, m_enabled? "yes":"no");
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
    delete cfg;
    cfg = nullptr;
}
void log_flush() {
    if (!cfg) {
        return;
    }
    cfg->flush();
}
