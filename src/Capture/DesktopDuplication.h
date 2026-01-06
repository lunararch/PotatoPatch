#pragma once
#include "../Core/D3D12Context.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <vector>
#include <string>

using Microsoft::WRL::ComPtr;

struct MonitorInfo
{
    HMONITOR hMonitor;
    std::wstring deviceName;
    RECT bounds;
    int outputIndex;
    int adapterIndex;
};

class DesktopDuplication
{
public:
    DesktopDuplication();
    ~DesktopDuplication();

    bool Initialize(D3D12Context* d3d12Context);
    void Shutdown();

    // Get list of available monitors
    std::vector<MonitorInfo> EnumerateMonitors();
    
    // Select which monitor to capture
    bool SelectMonitor(int monitorIndex);
    
    // Capture current frame - returns D3D11 texture
    // Returns true if new frame available
    bool CaptureFrame(int timeoutMs = 100);
    
    // Get the captured frame as D3D11 texture
    ID3D11Texture2D* GetCapturedTexture() { return m_capturedTexture.Get(); }
    
    // Get dimensions
    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }
    
    // Check if initialized and ready
    bool IsReady() const { return m_initialized; }

private:
    bool CreateD3D11Device();
    bool CreateDuplicationOutput(int adapterIndex, int outputIndex);
    
private:
    D3D12Context* m_d3d12Context = nullptr;
    
    // D3D11 resources for DXGI Desktop Duplication
    ComPtr<ID3D11Device> m_d3d11Device;
    ComPtr<ID3D11DeviceContext> m_d3d11Context;
    ComPtr<IDXGIOutputDuplication> m_duplication;
    ComPtr<ID3D11Texture2D> m_capturedTexture;
    
    // State
    bool m_initialized = false;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    int m_currentMonitor = -1;
    
    std::vector<MonitorInfo> m_monitors;
};
