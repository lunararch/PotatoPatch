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
        m_context->BeginFrame();

        ID3D12Resource* finalTexture = nullptr;

    // Capture window if enabled
    if (m_captureEnabled && m_targetWindow)
    {
        auto capturedTexture = m_capture->CaptureWindow(m_targetWindow);

        if (capturedTexture)
        {
            // Upscale if enabled
            if (m_upscaleEnabled)
            {
                finalTexture = m_upscaler->Upscale(capturedTexture, m_upscaleFactor);
            }
            else
            {
                finalTexture = capturedTexture;
            }
        }
    }

    // Display to backbuffer
    m_display->RenderToBackbuffer(finalTexture, m_context->GetCurrentBackBuffer());

    // Render UI (must happen before EndFrame, while backbuffer is in RENDER_TARGET state)
    RenderUI();

    m_context->EndFrame();

    // Calculate FPS
    m_fps = 1.0f / m_timer.GetDeltaTime();
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

    ImGui::Text("FPS: %.1f", m_fps);
    ImGui::Separator();

    // Window selection
    if (ImGui::Button("Select Window to Capture"))
    {
        // Simple window selection - in production you'd show a list
        m_targetWindow = FindWindow(nullptr, nullptr); // Gets active window
        if (m_targetWindow)
        {
            Logger::Info("Selected window for capture");
        }
    }

    ImGui::Checkbox("Enable Capture", &m_captureEnabled);

    ImGui::Separator();

    ImGui::Checkbox("Enable Upscaling", &m_upscaleEnabled);
    ImGui::SliderFloat("Upscale Factor", &m_upscaleFactor, 1.0f, 4.0f);

    ImGui::Separator();

    ImGui::Text("Capture: %s", m_captureEnabled ? "Active" : "Inactive");
    ImGui::Text("Target Window: %s", m_targetWindow ? "Selected" : "None");

    ImGui::End();

    m_ui->EndFrame();
    m_ui->Render(m_context->GetCurrentBackBuffer());
    }
    catch (const std::exception& e) {
        Logger::Error("RenderUI exception: %s", e.what());
        m_running = false;
    }
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