#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include "logging.h"
#include <shared_mutex>

class logger_config {
    public:
        logger_config() {
            init_debug_status();
        }
        bool enabled() {
            return m_enabled;
        }
        void init_debug_status() {
            int env_value_len = 16;
            size_t read_len;
            char *env_value = (char*) malloc(env_value_len * sizeof(char));
            _dupenv_s(&env_value, &read_len, "SCRCPY_DEBUG");
            printf("SCRCPY_DEBUG=%s\n", env_value);
            if (strlen(env_value) <= 0) {
                m_enabled = false;
            } else {
                m_enabled = true;
            }
            printf("SCRCPY_DEBUG=%s, debug output enabled? %s\n", env_value, m_enabled? "yes":"no");
            free(env_value);
        }
    private:
        bool m_enabled = false;
};

logger_config *cfg = new logger_config();

void debug_logf_actual(int line, char *file, char *fmt, ...) {
    if (!cfg->enabled()) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    printf("%s#%d ", file, line);
    vprintf(fmt, args);
}

void logging_cleanup() {
    delete cfg;
    cfg = nullptr;
}
