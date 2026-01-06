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
    wc.hCursor = nullptr;  // No cursor for overlay - let the underlying window show it
    wc.lpszClassName = L"PotatoPatchOverlay";
    wc.hbrBackground = nullptr;  // No background - we handle all painting
    
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
    // WS_EX_LAYERED - Required for proper transparency
    // WS_EX_TRANSPARENT - Click-through (critical for mouse input to reach game)
    // WS_EX_TOOLWINDOW - Doesn't show in taskbar
    // WS_EX_NOACTIVATE - Doesn't steal focus from game
    m_overlayHwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
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
    
    // Set layered window to be fully opaque but still click-through due to WS_EX_TRANSPARENT
    // LWA_ALPHA with 255 = fully visible, but WS_EX_TRANSPARENT handles click-through
    SetLayeredWindowAttributes(m_overlayHwnd, 0, 255, LWA_ALPHA);
    
    // CRITICAL: Hide cursor over this window to prevent double cursor
    // Desktop Duplication already captures the cursor in the desktop image,
    // so we don't want Windows to draw another cursor on top of our overlay
    // We'll handle this in WM_SETCURSOR by setting the cursor to NULL
    
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
    
    // Apply cached upscaling settings to the renderer
    m_renderer->SetUpscalingEnabled(m_upscaleEnabled);
    m_renderer->SetUpscaleMethod(m_upscaleMethod);
    m_renderer->SetUpscaleFactor(m_upscaleFactor);
    m_renderer->SetSharpness(m_sharpness);
    
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
    
    // Get window rect (includes borders for windowed mode)
    RECT windowRect;
    GetWindowRect(m_targetWindow, &windowRect);
    
    // Get client rect (actual drawable area)
    RECT clientRect;
    GetClientRect(m_targetWindow, &clientRect);
    
    // Calculate border sizes (difference between window and client)
    POINT clientTopLeft = { 0, 0 };
    ClientToScreen(m_targetWindow, &clientTopLeft);
    
    // For fullscreen/borderless windows, client and window coords should match
    // For windowed mode, we need to account for borders
    int borderLeft = clientTopLeft.x - windowRect.left;
    int borderTop = clientTopLeft.y - windowRect.top;
    
    // Calculate actual client area dimensions
    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;
    
    // Store the target rect for change detection
    m_targetRect = windowRect;
    
    // Position overlay exactly over the CLIENT area (where the game actually renders)
    SetWindowPos(
        m_overlayHwnd,
        HWND_TOPMOST,
        clientTopLeft.x,  // Use screen coordinates of client area
        clientTopLeft.y,
        width,
        height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW
    );
    
    // Resize renderer to match client area
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
    
    // Capture frame with minimal timeout for high performance (0ms = return immediately)
    // Desktop Duplication will return the latest frame or DXGI_ERROR_WAIT_TIMEOUT if no new frame
    bool hasNewFrame = m_capture->CaptureFrame(0);  // 0ms timeout = non-blocking
    
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
        // No new frame available - reuse the last captured frame
        // This allows us to maintain high overlay FPS even if desktop updates slowly
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
    
    // Critical: Always return HTTRANSPARENT for all hit testing
    // This ensures ALL mouse input passes through to the window below
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    
    // CRITICAL: Hide cursor over this window to prevent double cursor
    // Desktop Duplication captures cursor in the image, so we don't want Windows
    // to draw another cursor on top. Set cursor to NULL (invisible).\n    case WM_SETCURSOR:
        SetCursor(NULL);  // Hide the cursor completely
        return TRUE;  // Prevent default cursor handling
        
    // Ignore all mouse messages - they should go to the game
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        return 0;  // Swallow but don't process
    }
    
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Upscaling control methods
void OverlayWindow::SetUpscalingEnabled(bool enabled)
{
    m_upscaleEnabled = enabled;
    if (m_renderer)
    {
        m_renderer->SetUpscalingEnabled(enabled);
    }
}

bool OverlayWindow::IsUpscalingEnabled() const
{
    return m_upscaleEnabled;
}

void OverlayWindow::SetUpscaleMethod(UpscaleMethod method)
{
    m_upscaleMethod = method;
    if (m_renderer)
    {
        m_renderer->SetUpscaleMethod(method);
    }
}

UpscaleMethod OverlayWindow::GetUpscaleMethod() const
{
    return m_upscaleMethod;
}

void OverlayWindow::SetUpscaleFactor(float factor)
{
    m_upscaleFactor = factor;
    if (m_renderer)
    {
        m_renderer->SetUpscaleFactor(factor);
    }
}

float OverlayWindow::GetUpscaleFactor() const
{
    return m_upscaleFactor;
}

void OverlayWindow::SetSharpness(float sharpness)
{
    m_sharpness = sharpness;
    if (m_renderer)
    {
        m_renderer->SetSharpness(sharpness);
    }
}

float OverlayWindow::GetSharpness() const
{
    return m_sharpness;
}
