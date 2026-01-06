#include "Application.h"
#include "Utils/Logger.h"
#include <Windows.h>
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx12.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

Application::Application()
    : m_windowWidth(1280), m_windowHeight(720)
{
}

Application::~Application()
{
}

bool Application::Initialize(HINSTANCE hInstance, uint32_t width, uint32_t height)
{
    m_windowWidth = width;
    m_windowHeight = height;
    m_hInstance = hInstance;

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"PotatoPatch";

    if (!RegisterClassExW(&wc))
    {
        Logger::Error("Failed to register window class");
        return false;
    }

    // Create window
    RECT rect = { 0, 0, (LONG)width, (LONG)height };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    m_hwnd = CreateWindowExW(
        0,
        L"PotatoPatch",
        L"PotatoPatch",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        hInstance,
        this
    );

    if (!m_hwnd)
    {
        Logger::Error("Failed to create window");
        return false;
    }

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);

    m_running = true;

    // Initialize D3D12
    m_context = std::make_unique<D3D12Context>();
    if (!m_context->Initialize(m_hwnd, width, height))
    {
        Logger::Error("Failed to initialize D3D12 context");
        return false;
    }

    // Initialize subsystems
    m_capture = std::make_unique<CaptureEngine>();
    m_capture->Initialize(m_context.get());

    m_upscaler = std::make_unique<Upscaler>();
    m_upscaler->Initialize(m_context.get());

    m_display = std::make_unique<DisplayManager>();
    m_display->Initialize(m_context.get());

    m_ui = std::make_unique<ImGuiLayer>();
    m_ui->Initialize(m_hwnd, m_context.get());

    // Initialize overlay window system
    m_overlay = std::make_unique<OverlayWindow>();
    m_overlay->Initialize(hInstance);

    m_running = true;
    m_timer.Start();

    Logger::Info("Application initialized successfully");
    return true;
}

void Application::Run()
{
    while (m_running)
    {
        HandleWindowMessages();

        if (m_running)
        {
            ProcessFrame();
        }
    }
}

void Application::Shutdown()
{
    if (m_overlay) m_overlay->Shutdown();
    if (m_ui) m_ui->Shutdown();
    if (m_display) m_display->Shutdown();
    if (m_upscaler) m_upscaler->Shutdown();
    if (m_capture) m_capture->Shutdown();
    if (m_context) m_context->Shutdown();

    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    Logger::Info("Application shut down");
}

void Application::ProcessFrame()
{
    try {
        // If overlay mode is active, process overlay frames
        if (m_overlayMode && m_overlay && m_overlay->IsActive())
        {
            m_overlay->ProcessFrame();
            m_capturedFrames = m_overlay->GetFramesCaptured();
        }
        
        m_context->BeginFrame();

        ID3D12Resource* finalTexture = nullptr;

    // Regular capture mode (non-overlay) - just counts frames
    if (m_captureEnabled && !m_overlayMode && m_selectedMonitor >= 0 && m_capture->IsReady())
    {
        if (m_capture->CaptureFrame())
        {
            m_capturedFrames++;
        }
    }

    // Display to backbuffer
    m_display->RenderToBackbuffer(finalTexture, m_context->GetCurrentBackBuffer());

    // Render UI (must happen before EndFrame, while backbuffer is in RENDER_TARGET state)
    RenderUI();

    m_context->EndFrame();

    // Calculate FPS with smoothing
    float deltaTime = m_timer.GetDeltaTime();
    m_fps = 1.0f / deltaTime;
    
    // Update smoothed FPS every 0.5 seconds for readability
    m_fpsUpdateTimer += deltaTime;
    if (m_fpsUpdateTimer >= 0.5f)
    {
        m_fpsSmoothed = m_fps;
        m_fpsUpdateTimer = 0.0f;
    }
    
    m_timer.Tick();
    }
    catch (const std::exception& e) {
        Logger::Error("ProcessFrame exception: %s", e.what());
        m_running = false;
    }
}

void Application::RenderUI()
{
    try {
        m_ui->BeginFrame();

    // Main control window
    ImGui::Begin("PotatoPatch", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::Text("FPS (PotatoPatch): %.0f", m_fpsSmoothed);
    ImGui::Text("Captured Frames: %u", m_capturedFrames);
    ImGui::Separator();

    // Monitor selection
    ImGui::Text("Monitor Selection:");
    auto monitors = m_capture->GetMonitors();
    
    if (monitors.empty())
    {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "No monitors available for capture!");
    }
    else
    {
        for (size_t i = 0; i < monitors.size(); i++)
        {
            const auto& mon = monitors[i];
            char label[256];
            int width = mon.bounds.right - mon.bounds.left;
            int height = mon.bounds.bottom - mon.bounds.top;
            snprintf(label, sizeof(label), "Monitor %d: %dx%d", (int)i, width, height);
            
            bool isSelected = (m_selectedMonitor == (int)i);
            if (ImGui::RadioButton(label, isSelected))
            {
                if (m_capture->SelectMonitor((int)i))
                {
                    m_selectedMonitor = (int)i;
                    Logger::Info("Selected monitor %d for capture", (int)i);
                }
            }
        }
    }
    
    ImGui::Separator();
    
    // Window selection helper
    if (ImGui::Button("List All Windows"))
    {
        EnumerateAllWindows();
        m_showWindowList = true;
    }
    
    if (m_showWindowList && !m_availableWindows.empty())
    {
        ImGui::SameLine();
        if (ImGui::Button("Close List"))
        {
            m_showWindowList = false;
        }
        
        ImGui::BeginChild("WindowList", ImVec2(0, 200), true);
        ImGui::Text("Click a window to select it:");
        ImGui::Separator();
        
        for (const auto& window : m_availableWindows)
        {
            char titleUtf8[256];
            WideCharToMultiByte(CP_UTF8, 0, window.title.c_str(), -1, titleUtf8, sizeof(titleUtf8), nullptr, nullptr);
            
            if (ImGui::Selectable(titleUtf8))
            {
                m_targetWindow = window.hwnd;
                m_targetWindowTitle = window.title;
                
                RECT rect;
                GetClientRect(m_targetWindow, &rect);
                Logger::Info("Selected window '%s' (%dx%d)", titleUtf8, rect.right - rect.left, rect.bottom - rect.top);
                
                // Auto-select the monitor containing this window
                int monitorIndex = m_capture->GetMonitorForWindow(m_targetWindow);
                if (monitorIndex >= 0)
                {
                    if (m_capture->SelectMonitor(monitorIndex))
                    {
                        m_selectedMonitor = monitorIndex;
                        Logger::Info("Auto-selected monitor %d for window", monitorIndex);
                    }
                }
                
                m_showWindowList = false;
            }
        }
        ImGui::EndChild();
    }
    
    ImGui::Separator();
    
    // Manual search (in case window list doesn't work)
    static char windowTitleBuffer[256] = "";
    ImGui::InputText("Or search by title", windowTitleBuffer, sizeof(windowTitleBuffer));
    
    if (ImGui::Button("Find by Title"))
    {
        // Convert to wide string and find window
        int len = MultiByteToWideChar(CP_UTF8, 0, windowTitleBuffer, -1, nullptr, 0);
        m_targetWindowTitle.resize(len);
        MultiByteToWideChar(CP_UTF8, 0, windowTitleBuffer, -1, &m_targetWindowTitle[0], len);
        
        m_targetWindow = FindWindowW(nullptr, m_targetWindowTitle.c_str());
        if (m_targetWindow)
        {
            RECT rect;
            GetClientRect(m_targetWindow, &rect);
            Logger::Info("Found window '%s' (%dx%d)", windowTitleBuffer, rect.right - rect.left, rect.bottom - rect.top);
            
            // Auto-select the monitor containing this window
            int monitorIndex = m_capture->GetMonitorForWindow(m_targetWindow);
            if (monitorIndex >= 0)
            {
                if (m_capture->SelectMonitor(monitorIndex))
                {
                    m_selectedMonitor = monitorIndex;
                    Logger::Info("Auto-selected monitor %d for window", monitorIndex);
                }
            }
        }
        else
        {
            Logger::Error("Could not find window with title '%s'", windowTitleBuffer);
        }
    }

    ImGui::Separator();
    
    // OVERLAY MODE - the main feature!
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
    if (ImGui::CollapsingHeader("Overlay Mode (Like Lossless Scaling)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PopStyleColor();
        
        if (!m_overlayMode)
        {
            bool canStart = m_targetWindow && IsWindow(m_targetWindow) && m_selectedMonitor >= 0;
            
            if (!canStart)
            {
                ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Select a window first (use 'List All Windows')");
            }
            
            // Upscaling settings (configurable before starting overlay)
            ImGui::Checkbox("Enable Overlay Upscaling", &m_overlayUpscaleEnabled);
            
            if (m_overlayUpscaleEnabled)
            {
                ImGui::Indent();
                
                // Upscale method selection
                const char* methodNames[] = { "Bilinear", "FSR (Edge-Adaptive)" };
                int currentMethod = static_cast<int>(m_overlayUpscaleMethod);
                if (ImGui::Combo("Upscale Method", &currentMethod, methodNames, 2))
                {
                    m_overlayUpscaleMethod = static_cast<UpscaleMethod>(currentMethod);
                    if (m_overlay) m_overlay->SetUpscaleMethod(m_overlayUpscaleMethod);
                }
                
                // Upscale factor
                if (ImGui::SliderFloat("Upscale Factor", &m_overlayUpscaleFactor, 1.0f, 4.0f, "%.2fx"))
                {
                    if (m_overlay) m_overlay->SetUpscaleFactor(m_overlayUpscaleFactor);
                }
                
                // Sharpness (for FSR)
                if (m_overlayUpscaleMethod == UpscaleMethod::FSR)
                {
                    if (ImGui::SliderFloat("Sharpness", &m_overlaySharpness, 0.0f, 1.0f, "%.2f"))
                    {
                        if (m_overlay) m_overlay->SetSharpness(m_overlaySharpness);
                    }
                }
                
                ImGui::Unindent();
            }
            
            ImGui::BeginDisabled(!canStart);
            if (ImGui::Button("START OVERLAY", ImVec2(200, 40)))
            {
                StartOverlayMode();
            }
            ImGui::EndDisabled();
            
            ImGui::TextWrapped("This will capture the screen and display it in a borderless window on top of your game.");
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("STOP OVERLAY", ImVec2(200, 40)))
            {
                StopOverlayMode();
            }
            ImGui::PopStyleColor();
            
            ImGui::Text("Overlay FPS: %.0f", m_overlay ? m_overlay->GetOverlayFPS() : 0.0f);
            ImGui::Text("Frames Rendered: %u", m_capturedFrames);
            
            // Live upscaling controls while overlay is active
            ImGui::Separator();
            ImGui::Text("Live Upscaling Controls:");
            
            bool upscaleEnabled = m_overlay ? m_overlay->IsUpscalingEnabled() : false;
            if (ImGui::Checkbox("Upscaling Enabled", &upscaleEnabled))
            {
                if (m_overlay) m_overlay->SetUpscalingEnabled(upscaleEnabled);
                m_overlayUpscaleEnabled = upscaleEnabled;
            }
            
            if (upscaleEnabled)
            {
                const char* methodNames[] = { "Bilinear", "FSR (Edge-Adaptive)" };
                int currentMethod = static_cast<int>(m_overlayUpscaleMethod);
                if (ImGui::Combo("Method", &currentMethod, methodNames, 2))
                {
                    m_overlayUpscaleMethod = static_cast<UpscaleMethod>(currentMethod);
                    if (m_overlay) m_overlay->SetUpscaleMethod(m_overlayUpscaleMethod);
                }
                
                if (ImGui::SliderFloat("Factor", &m_overlayUpscaleFactor, 1.0f, 4.0f, "%.2fx"))
                {
                    if (m_overlay) m_overlay->SetUpscaleFactor(m_overlayUpscaleFactor);
                }
                
                if (m_overlayUpscaleMethod == UpscaleMethod::FSR)
                {
                    if (ImGui::SliderFloat("Sharpness", &m_overlaySharpness, 0.0f, 1.0f, "%.2f"))
                    {
                        if (m_overlay) m_overlay->SetSharpness(m_overlaySharpness);
                    }
                }
            }
            
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Overlay is ACTIVE!");
            ImGui::TextWrapped("Press ESC or click 'STOP OVERLAY' to stop.");
        }
    }
    else
    {
        ImGui::PopStyleColor();
    }

    ImGui::Separator();

    // Legacy capture mode (just counts frames, no display)
    ImGui::Checkbox("Enable Basic Capture (no display)", &m_captureEnabled);

    ImGui::Separator();

    ImGui::Checkbox("Enable Upscaling (Legacy)", &m_upscaleEnabled);
    ImGui::SliderFloat("Upscale Factor (Legacy)", &m_upscaleFactor, 1.0f, 4.0f);

    ImGui::Separator();
    
    // Status information
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 1.0f, 0.5f, 1.0f));
    ImGui::Text("Desktop Duplication capture active");
    ImGui::PopStyleColor();

    ImGui::Separator();

    ImGui::Text("Capture: %s", m_captureEnabled ? "Active" : "Inactive");
    ImGui::Text("Selected Monitor: %d", m_selectedMonitor);
    
    if (m_capture->IsReady())
    {
        ImGui::Text("Capture Size: %ux%u", m_capture->GetWidth(), m_capture->GetHeight());
    }
    
    if (m_targetWindow && IsWindow(m_targetWindow))
    {
        char title[256];
        WideCharToMultiByte(CP_UTF8, 0, m_targetWindowTitle.c_str(), -1, title, sizeof(title), nullptr, nullptr);
        ImGui::Text("Target Window: %s", title);
    }

    ImGui::End();

    m_ui->EndFrame();
    m_ui->Render(m_context->GetCurrentBackBuffer());
    }
    catch (const std::exception& e) {
        Logger::Error("RenderUI exception: %s", e.what());
        m_running = false;
    }
}

void Application::StartOverlayMode()
{
    if (!m_targetWindow || !IsWindow(m_targetWindow))
    {
        Logger::Error("Cannot start overlay: no target window selected");
        return;
    }
    
    if (!m_capture || !m_capture->IsReady())
    {
        Logger::Error("Cannot start overlay: capture not ready. Select a monitor first.");
        return;
    }
    
    // Get desktop duplication from capture engine
    auto* desktopDup = m_capture->GetDesktopDuplication();
    if (!desktopDup)
    {
        Logger::Error("Cannot start overlay: no desktop duplication available");
        return;
    }
    
    // Apply upscaling settings before starting
    m_overlay->SetUpscalingEnabled(m_overlayUpscaleEnabled);
    m_overlay->SetUpscaleMethod(m_overlayUpscaleMethod);
    m_overlay->SetUpscaleFactor(m_overlayUpscaleFactor);
    m_overlay->SetSharpness(m_overlaySharpness);
    
    // Set target window for overlay
    m_overlay->SetTargetWindow(m_targetWindow);
    
    // Start overlay
    if (m_overlay->StartOverlay(desktopDup))
    {
        m_overlayMode = true;
        m_capturedFrames = 0;
        Logger::Info("Overlay mode started with upscaling: %s (%.2fx, method=%d)", 
            m_overlayUpscaleEnabled ? "enabled" : "disabled",
            m_overlayUpscaleFactor,
            static_cast<int>(m_overlayUpscaleMethod));
    }
    else
    {
        Logger::Error("Failed to start overlay mode");
    }
}

void Application::StopOverlayMode()
{
    if (m_overlay)
    {
        m_overlay->StopOverlay();
    }
    m_overlayMode = false;
    Logger::Info("Overlay mode stopped");
}

void Application::HandleWindowMessages()
{
    MSG msg = {};
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        if (msg.message == WM_QUIT)
        {
            m_running = false;
        }
    }
}

BOOL CALLBACK Application::EnumWindowsCallback(HWND hwnd, LPARAM lParam)
{
    Application* app = reinterpret_cast<Application*>(lParam);
    
    if (!IsWindowVisible(hwnd))
        return TRUE;
    
    wchar_t title[256];
    GetWindowTextW(hwnd, title, sizeof(title) / sizeof(wchar_t));
    
    if (wcslen(title) > 0)
    {
        WindowInfo info;
        info.title = title;
        info.hwnd = hwnd;
        app->m_availableWindows.push_back(info);
    }
    
    return TRUE;
}

void Application::EnumerateAllWindows()
{
    m_availableWindows.clear();
    EnumWindows(EnumWindowsCallback, reinterpret_cast<LPARAM>(this));
}

LRESULT CALLBACK Application::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam))
        return true;

    Application* app = nullptr;

    if (uMsg == WM_CREATE)
    {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        app = reinterpret_cast<Application*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    else
    {
        app = reinterpret_cast<Application*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    switch (uMsg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_SIZE:
        if (app && app->m_context)
        {
            UINT width = LOWORD(lParam);
            UINT height = HIWORD(lParam);
            app->m_context->Resize(width, height);
        }
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}