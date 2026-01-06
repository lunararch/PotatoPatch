#pragma once
#include "../Core/D3D12Context.h"
#include "DesktopDuplication.h"
#include <Windows.h>
#include <memory>
#include <vector>

class CaptureEngine
{
public:
    CaptureEngine();
    ~CaptureEngine();

    bool Initialize(D3D12Context* context);
    void Shutdown();

    // Get available monitors for capture
    std::vector<MonitorInfo> GetMonitors();
    
    // Select which monitor to capture (call before enabling capture)
    bool SelectMonitor(int monitorIndex);
    
    // Get monitor that contains a window
    int GetMonitorForWindow(HWND hwnd);
    
    // Capture the selected monitor - returns true if frame captured
    bool CaptureFrame();
    
    // Get the last captured frame (D3D11 texture - not yet converted to D3D12)
    ID3D11Texture2D* GetLastCapturedD3D11Texture();
    
    // Legacy method for compatibility
    ID3D12Resource* CaptureWindow(HWND targetWindow);
    
    // Get capture dimensions
    uint32_t GetWidth() const;
    uint32_t GetHeight() const;
    
    // Check if ready
    bool IsReady() const;
    
    int GetSelectedMonitor() const { return m_selectedMonitor; }
    
    // Get desktop duplication for overlay system
    DesktopDuplication* GetDesktopDuplication() { return m_desktopDuplication.get(); }

private:
    D3D12Context* m_context = nullptr;
    std::unique_ptr<DesktopDuplication> m_desktopDuplication;
    int m_selectedMonitor = -1;
};