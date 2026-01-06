#include "OverlayRenderer.h"
#include "../Utils/Logger.h"

OverlayRenderer::OverlayRenderer()
{
}

OverlayRenderer::~OverlayRenderer()
{
    Shutdown();
}

bool OverlayRenderer::Initialize(HWND overlayWindow, ID3D11Device* captureDevice, ID3D11DeviceContext* captureContext)
{
    m_device = captureDevice;
    m_context = captureContext;
    
    if (!CreateSwapChain(overlayWindow))
    {
        Logger::Error("Failed to create overlay swap chain");
        return false;
    }
    
    if (!CreateRenderTarget())
    {
        Logger::Error("Failed to create overlay render target");
        return false;
    }
    
    Logger::Info("Overlay renderer initialized");
    return true;
}

void OverlayRenderer::Shutdown()
{
    ReleaseRenderTarget();
    m_swapChain.Reset();
    m_device = nullptr;
    m_context = nullptr;
}

bool OverlayRenderer::CreateSwapChain(HWND hwnd)
{
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = m_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (FAILED(hr)) return false;
    
    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) return false;
    
    ComPtr<IDXGIFactory2> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return false;
    
    RECT rect;
    GetClientRect(hwnd, &rect);
    m_width = rect.right - rect.left;
    m_height = rect.bottom - rect.top;
    
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    
    hr = factory->CreateSwapChainForHwnd(
        m_device,
        hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &m_swapChain
    );
    
    if (FAILED(hr))
    {
        // Try without tearing support
        swapChainDesc.Flags = 0;
        hr = factory->CreateSwapChainForHwnd(
            m_device,
            hwnd,
            &swapChainDesc,
            nullptr,
            nullptr,
            &m_swapChain
        );
    }
    
    if (FAILED(hr))
    {
        Logger::Error("CreateSwapChainForHwnd failed: 0x%08X", hr);
        return false;
    }
    
    // Disable ALT+ENTER fullscreen
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    
    return true;
}

bool OverlayRenderer::CreateRenderTarget()
{
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&m_backBuffer));
    if (FAILED(hr)) return false;
    
    hr = m_device->CreateRenderTargetView(m_backBuffer.Get(), nullptr, &m_renderTargetView);
    if (FAILED(hr)) return false;
    
    return true;
}

void OverlayRenderer::ReleaseRenderTarget()
{
    if (m_context)
    {
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
    }
    m_renderTargetView.Reset();
    m_backBuffer.Reset();
}

void OverlayRenderer::RenderFrame(ID3D11Texture2D* capturedFrame)
{
    if (!m_backBuffer) return;
    
    // Clear the back buffer to black (prevents ghosting)
    if (m_renderTargetView)
    {
        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        m_context->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);
    }
    
    // If no captured frame, just present the cleared buffer
    if (!capturedFrame) return;
    
    // Get captured frame dimensions
    D3D11_TEXTURE2D_DESC srcDesc;
    capturedFrame->GetDesc(&srcDesc);
    
    // Get back buffer dimensions  
    D3D11_TEXTURE2D_DESC dstDesc;
    m_backBuffer->GetDesc(&dstDesc);
    
    // Both should be BGRA format
    if (srcDesc.Format != dstDesc.Format)
    {
        Logger::Warning("Format mismatch: src=%d dst=%d", srcDesc.Format, dstDesc.Format);
        return;
    }
    
    // Copy the captured frame to back buffer
    // Use CopyResource for full texture copy when sizes match
    if (srcDesc.Width == dstDesc.Width && srcDesc.Height == dstDesc.Height)
    {
        m_context->CopyResource(m_backBuffer.Get(), capturedFrame);
    }
    else
    {
        // If sizes don't match, copy what fits
        D3D11_BOX srcBox = {};
        srcBox.left = 0;
        srcBox.top = 0;
        srcBox.front = 0;
        srcBox.right = min(srcDesc.Width, dstDesc.Width);
        srcBox.bottom = min(srcDesc.Height, dstDesc.Height);
        srcBox.back = 1;
        
        m_context->CopySubresourceRegion(
            m_backBuffer.Get(), 0,
            0, 0, 0,
            capturedFrame, 0,
            &srcBox
        );
    }
    
    // Flush to ensure copy completes before present
    m_context->Flush();
}

void OverlayRenderer::Present(bool vsync)
{
    if (m_swapChain)
    {
        // Use DXGI_PRESENT_ALLOW_TEARING for lowest latency when not vsync
        UINT flags = vsync ? 0 : DXGI_PRESENT_ALLOW_TEARING;
        UINT syncInterval = vsync ? 1 : 0;
        
        HRESULT hr = m_swapChain->Present(syncInterval, flags);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        {
            Logger::Error("Device lost during present");
        }
    }
}

void OverlayRenderer::Resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) return;
    if (width == m_width && height == m_height) return;
    
    m_width = width;
    m_height = height;
    
    ReleaseRenderTarget();
    
    HRESULT hr = m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
    if (FAILED(hr))
    {
        hr = m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    }
    
    if (SUCCEEDED(hr))
    {
        CreateRenderTarget();
    }
}
