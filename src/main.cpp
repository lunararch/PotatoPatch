#include "Application.h"
#include "Utils/Logger.h"
#include <Windows.h>
#include <exception>

int main()
{
    HINSTANCE hInstance = GetModuleHandle(nullptr);
    
    Logger::Init();
    Logger::Info("Starting PotatoPatch...");

    try
    {
        Application app;
        if (!app.Initialize(hInstance, 1280, 720))
        {
            Logger::Error("Failed to initialize application");
            return -1;
        }

        app.Run();
        app.Shutdown();
    }
    catch (const std::exception& e)
    {
        Logger::Error("Exception: %s", e.what());
        MessageBoxA(nullptr, e.what(), "Fatal Error", MB_OK | MB_ICONERROR);
        return -1;
    }
    catch (...)
    {
        Logger::Error("Unknown exception occurred");
        MessageBoxA(nullptr, "Unknown exception occurred", "Fatal Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    Logger::Info("Application closed successfully");
    return 0;
}