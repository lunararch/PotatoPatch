#include "DesktopDuplication.h"
#include "../Utils/Logger.h"
#include <d3dx12.h>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")

DesktopDuplication::DesktopDuplication()
{
}

DesktopDuplication::~DesktopDuplication()
{
    Shutdown();
}

bool DesktopDuplication::Initialize(D3D12Context* d3d12Context)
{
    m_d3d12Context = d3d12Context;
    
    if (!CreateD3D11Device())
    {
        Logger::Error("Failed to create D3D11 device for desktop duplication");
        return false;
    }
    
    // Enumerate available monitors
    m_monitors = EnumerateMonitors();
    
    if (m_monitors.empty())
    {
        Logger::Error("No monitors found for desktop duplication");
        return false;
    }
    
    Logger::Info("Desktop duplication initialized with %d monitors", (int)m_monitors.size());
    return true;
}

void DesktopDuplication::Shutdown()
{
    m_duplication.Reset();
    m_capturedTexture.Reset();
    m_d3d11Context.Reset();
    m_d3d11Device.Reset();
    m_initialized = false;
    m_currentMonitor = -1;
}

bool DesktopDuplication::CreateD3D11Device()
{
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    
    D3D_FEATURE_LEVEL featureLevel;
    
    HRESULT hr = D3D11CreateDevice(
        nullptr,                    // Default adapter
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &m_d3d11Device,
        &featureLevel,
        &m_d3d11Context
    );
    
    if (FAILED(hr))
    {
        Logger::Error("D3D11CreateDevice failed: 0x%08X", hr);
        return false;
    }
    
    Logger::Info("D3D11 device created successfully");
    return true;
}

std::vector<MonitorInfo> DesktopDuplication::EnumerateMonitors()
{
    std::vector<MonitorInfo> monitors;
    
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = m_d3d11Device.As(&dxgiDevice);
    if (FAILED(hr)) return monitors;
    
    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) return monitors;
    
    // Enumerate all adapters
    ComPtr<IDXGIFactory1> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return monitors;
    
    for (UINT adapterIndex = 0; ; adapterIndex++)
    {
        ComPtr<IDXGIAdapter1> currentAdapter;
        hr = factory->EnumAdapters1(adapterIndex, &currentAdapter);
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        
        // Enumerate outputs for this adapter
        for (UINT outputIndex = 0; ; outputIndex++)
        {
            ComPtr<IDXGIOutput> output;
            hr = currentAdapter->EnumOutputs(outputIndex, &output);
            if (hr == DXGI_ERROR_NOT_FOUND) break;
            
            DXGI_OUTPUT_DESC desc;
            output->GetDesc(&desc);
            
            MonitorInfo info;
            info.hMonitor = desc.Monitor;
            info.deviceName = desc.DeviceName;
            info.bounds = desc.DesktopCoordinates;
            info.outputIndex = outputIndex;
            info.adapterIndex = adapterIndex;
            
            monitors.push_back(info);
            
            Logger::Info("Found monitor %d: %ws (%dx%d)", 
                (int)monitors.size() - 1,
                desc.DeviceName,
                desc.DesktopCoordinates.right - desc.DesktopCoordinates.left,
                desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top);
        }
    }
    
    return monitors;
}

bool DesktopDuplication::SelectMonitor(int monitorIndex)
{
    if (monitorIndex < 0 || monitorIndex >= (int)m_monitors.size())
    {
        Logger::Error("Invalid monitor index: %d", monitorIndex);
        return false;
    }
    
    // Release existing duplication
    m_duplication.Reset();
    m_capturedTexture.Reset();
    m_initialized = false;
    
    const MonitorInfo& monitor = m_monitors[monitorIndex];
    
    if (!CreateDuplicationOutput(monitor.adapterIndex, monitor.outputIndex))
    {
        return false;
    }
    
    m_currentMonitor = monitorIndex;
    m_width = monitor.bounds.right - monitor.bounds.left;
    m_height = monitor.bounds.bottom - monitor.bounds.top;
    m_initialized = true;
    
    Logger::Info("Selected monitor %d for capture (%dx%d)", monitorIndex, m_width, m_height);
    return true;
}

bool DesktopDuplication::CreateDuplicationOutput(int adapterIndex, int outputIndex)
{
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = m_d3d11Device.As(&dxgiDevice);
    if (FAILED(hr)) return false;
    
    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) return false;
    
    ComPtr<IDXGIFactory1> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return false;
    
    ComPtr<IDXGIAdapter1> targetAdapter;
    hr = factory->EnumAdapters1(adapterIndex, &targetAdapter);
    if (FAILED(hr))
    {
        Logger::Error("Failed to get adapter %d", adapterIndex);
        return false;
    }
    
    ComPtr<IDXGIOutput> output;
    hr = targetAdapter->EnumOutputs(outputIndex, &output);
    if (FAILED(hr))
    {
        Logger::Error("Failed to get output %d", outputIndex);
        return false;
    }
    
    ComPtr<IDXGIOutput1> output1;
    hr = output.As(&output1);
    if (FAILED(hr))
    {
        Logger::Error("Failed to get IDXGIOutput1");
        return false;
    }
    
    hr = output1->DuplicateOutput(m_d3d11Device.Get(), &m_duplication);
    if (FAILED(hr))
    {
        if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE)
        {
            Logger::Error("Desktop duplication not available (another app may be using it)");
        }
        else if (hr == E_ACCESSDENIED)
        {
            Logger::Error("Access denied - try running as administrator or from a desktop session");
        }
        else
        {
            Logger::Error("DuplicateOutput failed: 0x%08X", hr);
        }
        return false;
    }
    
    DXGI_OUTPUT_DESC desc;
    output->GetDesc(&desc);
    
    // Create texture for captured frames
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
    texDesc.Height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = 0;
    
    hr = m_d3d11Device->CreateTexture2D(&texDesc, nullptr, &m_capturedTexture);
    if (FAILED(hr))
    {
        Logger::Error("Failed to create capture texture: 0x%08X", hr);
        return false;
    }
    
    Logger::Info("Desktop duplication output created successfully");
    return true;
}

bool DesktopDuplication::CaptureFrame(int timeoutMs)
{
    if (!m_initialized || !m_duplication)
    {
        return false;
    }
    
    ComPtr<IDXGIResource> desktopResource;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    
    HRESULT hr = m_duplication->AcquireNextFrame(timeoutMs, &frameInfo, &desktopResource);
    
    if (hr == DXGI_ERROR_WAIT_TIMEOUT)
    {
        // No new frame, but that's okay
        return false;
    }
    
    if (hr == DXGI_ERROR_ACCESS_LOST)
    {
        Logger::Warning("Desktop duplication access lost, reinitializing...");
        // Need to recreate duplication
        if (m_currentMonitor >= 0)
        {
            SelectMonitor(m_currentMonitor);
        }
        return false;
    }
    
    if (FAILED(hr))
    {
        Logger::Error("AcquireNextFrame failed: 0x%08X", hr);
        return false;
    }
    
    // Get the texture
    ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource.As(&desktopTexture);
    if (FAILED(hr))
    {
        m_duplication->ReleaseFrame();
        return false;
    }
    
    // Copy to our texture
    m_d3d11Context->CopyResource(m_capturedTexture.Get(), desktopTexture.Get());
    
    m_duplication->ReleaseFrame();
    return true;
}

// Note: We now return D3D11 textures directly instead of copying to D3D12
// This is much more efficient and avoids texture creation errors
// The processing pipeline will need to handle D3D11 textures or use interop
