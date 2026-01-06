#include "CaptureEngine.h"
#include "../Utils/Logger.h"

CaptureEngine::CaptureEngine()
{
}

CaptureEngine::~CaptureEngine()
{
    Shutdown();
}

bool CaptureEngine::Initialize(D3D12Context* context)
{
    m_context = context;
    
    m_desktopDuplication = std::make_unique<DesktopDuplication>();
    if (!m_desktopDuplication->Initialize(context))
    {
        Logger::Error("Failed to initialize desktop duplication");
        return false;
    }
    
    Logger::Info("Capture engine initialized with desktop duplication");
    return true;
}

void CaptureEngine::Shutdown()
{
    if (m_desktopDuplication)
    {
        m_desktopDuplication->Shutdown();
        m_desktopDuplication.reset();
    }
}

std::vector<MonitorInfo> CaptureEngine::GetMonitors()
{
    // Return cached monitors from initialization
    if (m_desktopDuplication)
    {
        // Cache the monitors on first call to avoid re-enumeration
        static std::vector<MonitorInfo> cachedMonitors;
        static bool cached = false;
        
        if (!cached)
        {
            cachedMonitors = m_desktopDuplication->EnumerateMonitors();
            cached = true;
        }
        return cachedMonitors;
    }
    return {};
}

bool CaptureEngine::SelectMonitor(int monitorIndex)
{
    if (!m_desktopDuplication)
    {
        return false;
    }
    
    if (m_desktopDuplication->SelectMonitor(monitorIndex))
    {
        m_selectedMonitor = monitorIndex;
        return true;
    }
    return false;
}

int CaptureEngine::GetMonitorForWindow(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
    {
        return -1;
    }
    
    HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!hMonitor)
    {
        return -1;
    }
    
    auto monitors = GetMonitors();
    for (size_t i = 0; i < monitors.size(); i++)
    {
        if (monitors[i].hMonitor == hMonitor)
        {
            return (int)i;
        }
    }
    
    return -1;
}

bool CaptureEngine::CaptureFrame()
{
    if (!m_desktopDuplication || !m_desktopDuplication->IsReady())
    {
        return false;
    }
    
    // Capture the frame (stored in D3D11 texture)
    return m_desktopDuplication->CaptureFrame(16);  // ~60fps timeout
}

ID3D11Texture2D* CaptureEngine::GetLastCapturedD3D11Texture()
{
    if (m_desktopDuplication)
    {
        return m_desktopDuplication->GetCapturedTexture();
    }
    return nullptr;
}

ID3D12Resource* CaptureEngine::CaptureWindow(HWND targetWindow)
{
    // For window capture, we capture the monitor containing the window
    if (targetWindow && IsWindow(targetWindow))
    {
        int monitorIndex = GetMonitorForWindow(targetWindow);
        if (monitorIndex >= 0 && monitorIndex != m_selectedMonitor)
        {
            SelectMonitor(monitorIndex);
        }
    }
    
    CaptureFrame();
    return nullptr; // TODO: Return D3D12 texture once interop is implemented
}

uint32_t CaptureEngine::GetWidth() const
{
    if (m_desktopDuplication)
    {
        return m_desktopDuplication->GetWidth();
    }
    return 0;
}

uint32_t CaptureEngine::GetHeight() const
{
    if (m_desktopDuplication)
    {
        return m_desktopDuplication->GetHeight();
    }
    return 0;
}

bool CaptureEngine::IsReady() const
{
    return m_desktopDuplication && m_desktopDuplication->IsReady();
}