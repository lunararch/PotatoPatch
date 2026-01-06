#include "OverlayWindow.h"
#include "../Utils/Logger.h"
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

// WDA_EXCLUDEFROMCAPTURE is available on Windows 10 version 2004 (build 19041) and later
// Define it here for older SDKs
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

OverlayWindow::OverlayWindow()
{
    QueryPerformanceFrequency(&m_frequency);
    QueryPerformanceCounter(&m_lastFrameTime);
}

OverlayWindow::~OverlayWindow()
{
    Shutdown();
}

bool OverlayWindow::Initialize(HINSTANCE hInstance)
{
    if (!CreateOverlayWindow(hInstance))
    {
        Logger::Error("Failed to create overlay window");
        return false;
    }
    
    Logger::Info("Overlay window system initialized");
    return true;
}

void OverlayWindow::Shutdown()
{
    StopOverlay();
    
    if (m_overlayHwnd)
    {
        DestroyWindow(m_overlayHwnd);
        m_overlayHwnd = nullptr;
    }
}

bool OverlayWindow::CreateOverlayWindow(HINSTANCE hInstance)
{
    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"PotatoPatchOverlay";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    
    if (!RegisterClassExW(&wc))
    {
        // Class might already be registered
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            Logger::Error("Failed to register overlay window class");
            return false;
        }
    }
    
    // Create the overlay window - starts hidden
    // WS_EX_TOPMOST - Always on top
    // WS_EX_LAYERED - Allows transparency (optional)
    // WS_EX_TOOLWINDOW - Doesn't show in taskbar
    // WS_EX_NOACTIVATE - Doesn't steal focus from game
    m_overlayHwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"PotatoPatchOverlay",
        L"PotatoPatch Overlay",
        WS_POPUP,  // Borderless
        0, 0, 800, 600,
        nullptr,
        nullptr,
        hInstance,
        this
    );
    
    if (!m_overlayHwnd)
    {
        Logger::Error("Failed to create overlay window");
        return false;
    }
    
    // CRITICAL: Exclude this window from Desktop Duplication capture!
    // SetWindowDisplayAffinity with WDA_EXCLUDEFROMCAPTURE (Windows 10 2004+)
    // This is the proper Windows API for excluding windows from screen capture
    // The window will be visible normally but won't appear in Desktop Duplication
    if (!SetWindowDisplayAffinity(m_overlayHwnd, WDA_EXCLUDEFROMCAPTURE))
    {
        // WDA_EXCLUDEFROMCAPTURE might not be available on older Windows
        // Fall back to WDA_MONITOR which shows the window as black in capture
        DWORD error = GetLastError();
        Logger::Warning("WDA_EXCLUDEFROMCAPTURE failed (error %d), trying WDA_MONITOR", error);
        
        if (!SetWindowDisplayAffinity(m_overlayHwnd, WDA_MONITOR))
        {
            Logger::Warning("SetWindowDisplayAffinity failed - overlay may cause feedback loop");
        }
    }
    else
    {
        Logger::Info("Overlay window excluded from screen capture");
    }
    
    // Store this pointer
    SetWindowLongPtr(m_overlayHwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    
    return true;
}

bool OverlayWindow::SetTargetWindow(HWND targetWindow)
{
    if (!targetWindow || !IsWindow(targetWindow))
    {
        Logger::Error("Invalid target window");
        return false;
    }
    
    m_targetWindow = targetWindow;
    
    // Get target window position
    GetWindowRect(m_targetWindow, &m_targetRect);
    
    Logger::Info("Target window set: %dx%d at (%d,%d)",
        m_targetRect.right - m_targetRect.left,
        m_targetRect.bottom - m_targetRect.top,
        m_targetRect.left, m_targetRect.top);
    
    return true;
}

bool OverlayWindow::StartOverlay(DesktopDuplication* capture)
{
    if (!m_overlayHwnd || !m_targetWindow || !capture)
    {
        Logger::Error("Cannot start overlay: missing components");
        return false;
    }
    
    m_capture = capture;
    
    // Position overlay over target
    PositionOverlayOverTarget();
    
    // Use the D3D11 device from the capture system (CRITICAL - must be same device!)
    ID3D11Device* captureDevice = capture->GetD3D11Device();
    ID3D11DeviceContext* captureContext = capture->GetD3D11Context();
    
    if (!captureDevice || !captureContext)
    {
        Logger::Error("Failed to get D3D11 device from capture");
        return false;
    }
    
    m_renderer = std::make_unique<OverlayRenderer>();
    
    if (!m_renderer->Initialize(m_overlayHwnd, captureDevice, captureContext))
    {
        Logger::Error("Failed to initialize overlay renderer");
        return false;
    }
    
    // Show the overlay window
    ShowWindow(m_overlayHwnd, SW_SHOWNOACTIVATE);
    
    m_overlayActive = true;
    m_framesCaptured = 0;
    
    Logger::Info("Overlay started!");
    return true;
}

void OverlayWindow::StopOverlay()
{
    m_overlayActive = false;
    
    if (m_overlayHwnd)
    {
        ShowWindow(m_overlayHwnd, SW_HIDE);
    }
    
    if (m_renderer)
    {
        m_renderer->Shutdown();
        m_renderer.reset();
    }
    
    m_capture = nullptr;
    Logger::Info("Overlay stopped");
}

void OverlayWindow::UpdatePosition()
{
    if (!m_overlayActive || !m_targetWindow)
        return;
    
    // Check if target window still exists
    if (!IsWindow(m_targetWindow))
    {
        Logger::Warning("Target window no longer exists, stopping overlay");
        StopOverlay();
        return;
    }
    
    // Get current target position
    RECT newRect;
    GetWindowRect(m_targetWindow, &newRect);
    
    // Only update if changed
    if (memcmp(&newRect, &m_targetRect, sizeof(RECT)) != 0)
    {
        m_targetRect = newRect;
        PositionOverlayOverTarget();
    }
}

void OverlayWindow::PositionOverlayOverTarget()
{
    if (!m_overlayHwnd || !m_targetWindow)
        return;
    
    GetWindowRect(m_targetWindow, &m_targetRect);
    
    int width = m_targetRect.right - m_targetRect.left;
    int height = m_targetRect.bottom - m_targetRect.top;
    
    // Position overlay exactly over target window
    SetWindowPos(
        m_overlayHwnd,
        HWND_TOPMOST,
        m_targetRect.left,
        m_targetRect.top,
        width,
        height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW
    );
    
    // Resize renderer if needed
    if (m_renderer)
    {
        m_renderer->Resize(width, height);
    }
}

void OverlayWindow::ProcessFrame()
{
    if (!m_overlayActive || !m_capture || !m_renderer)
        return;
    
    // Update overlay position to track target window
    UpdatePosition();
    
    // NOTE: The overlay window is excluded from Desktop Duplication capture via
    // SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE), so we don't need to hide it.
    // This API properly excludes our window from the captured desktop image.
    
    // Capture frame (try to get new frame)
    bool hasNewFrame = m_capture->CaptureFrame(8);  // Short timeout for low latency
    
    ID3D11Texture2D* capturedFrame = nullptr;
    if (hasNewFrame)
    {
        capturedFrame = m_capture->GetCapturedTexture();
        if (capturedFrame)
        {
            m_framesCaptured++;
        }
    }
    else
    {
        // No new frame, but still get the last captured frame to keep displaying
        capturedFrame = m_capture->GetCapturedTexture();
    }
    
    // Always render and present (even if no new frame, to avoid ghosting)
    m_renderer->RenderFrame(capturedFrame);
    m_renderer->Present(false);  // No vsync for lowest latency
    
    // Calculate FPS
    LARGE_INTEGER currentTime;
    QueryPerformanceCounter(&currentTime);
    
    float deltaTime = (float)(currentTime.QuadPart - m_lastFrameTime.QuadPart) / m_frequency.QuadPart;
    m_lastFrameTime = currentTime;
    
    m_fpsAccumulator += deltaTime;
    m_fpsFrameCount++;
    
    if (m_fpsAccumulator >= 0.5f)
    {
        m_overlayFPS = m_fpsFrameCount / m_fpsAccumulator;
        m_fpsAccumulator = 0.0f;
        m_fpsFrameCount = 0;
    }
}

LRESULT CALLBACK OverlayWindow::OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    OverlayWindow* overlay = reinterpret_cast<OverlayWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    
    switch (msg)
    {
    case WM_DESTROY:
        return 0;
        
    case WM_SIZE:
        if (overlay && overlay->m_renderer)
        {
            UINT width = LOWORD(lParam);
            UINT height = HIWORD(lParam);
            overlay->m_renderer->Resize(width, height);
        }
        return 0;
        
    // Pass through mouse/keyboard to the window below
    case WM_NCHITTEST:
        return HTTRANSPARENT;  // Click-through
    }
    
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
