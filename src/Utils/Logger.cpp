#include "Logger.h"
#include <cstdarg>
#include <ctime>

void Logger::Init()
{
}

void Logger::Info(const char* format, ...)
{
    printf("[INFO] ");
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

void Logger::Warning(const char* format, ...)
{
    printf("[WARNING] ");
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

void Logger::Error(const char* format, ...)
{
    printf("[ERROR] ");
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}