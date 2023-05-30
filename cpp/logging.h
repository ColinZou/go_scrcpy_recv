#ifndef SCRCPY_LOGGING
#define SCRCPY_LOGGING
#ifdef __cplusplus
#ifndef SCRCPY_LOGGING_METHOD
#define SCRCPY_LOGGING_METHOD
void debug_logf_actual(int line, char *file, char *fmt, ...);
void logging_cleanup();
#define debug_logf(format, ...)                                                \
  debug_logf_actual(__LINE__, (char *)&__FILE__, (char *) &format, ##__VA_ARGS__)
#endif //!SCRCPY_LOGGING_METHOD
#endif //!__cplusplus
#endif //!SCRCPY_LOGGING
