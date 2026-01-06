#pragma once
#include <cstdio>

class Logger
{
public:
    static void Init();
    static void Info(const char* format, ...);
    static void Warning(const char* format, ...);
    static void Error(const char* format, ...);
};