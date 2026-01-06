#pragma once

#include <Windows.h>
#include <memory>
#include <string>
#include "Core/D3D12Context.h"
#include "Capture/CaptureEngine.h"
#include "Processing/Upscaler.h"
#include "Display/DisplayManager.h"
#include "UI/ImGuiLayer.h"
#include "Utils/Timer.h"

class Application
{
public:
    Application();
    ~Application();

    bool Initialize(HINSTANCE hInstance, uint32_t width, uint32_t height);
    void Run();
    void Shutdown();

private:
    void ProcessFrame();
    void RenderUI();
    void HandleWindowMessages();
    void EnumerateAllWindows();

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam);

private:
    HWND m_hwnd = nullptr;
    uint32_t m_windowWidth;
    uint32_t m_windowHeight;
    bool m_running = false;

    // Core systems
    std::unique_ptr<D3D12Context> m_context;
    std::unique_ptr<CaptureEngine> m_capture;
    std::unique_ptr<Upscaler> m_upscaler;
    std::unique_ptr<DisplayManager> m_display;
    std::unique_ptr<ImGuiLayer> m_ui;
    
    // UI state
    bool m_captureEnabled = false;
    bool m_upscaleEnabled = true;
    float m_upscaleFactor = 2.0f;
    HWND m_targetWindow = nullptr;
    int m_selectedMonitor = -1;
    
    // Performance tracking
    Timer m_timer;
    float m_fps = 0.0f;
    float m_fpsSmoothed = 0.0f;
    float m_fpsUpdateTimer = 0.0f;
    std::wstring m_targetWindowTitle;
    
    // Frame counter for captured frames
    uint32_t m_capturedFrames = 0;
    
    // Window enumeration
    struct WindowInfo {
        std::wstring title;
        HWND hwnd;
    };
    std::vector<WindowInfo> m_availableWindows;
    bool m_showWindowList = false;
};
