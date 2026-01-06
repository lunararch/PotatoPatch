#include "OverlayRenderer.h"
#include "../Utils/Logger.h"
#include <dxgi1_5.h>  // For IDXGIFactory5 and DXGI_FEATURE_PRESENT_ALLOW_TEARING

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
    
    // Initialize the D3D11 upscaler
    m_upscaler = std::make_unique<D3D11Upscaler>();
    if (!m_upscaler->Initialize(m_device, m_context))
    {
        Logger::Warning("Failed to initialize D3D11 upscaler - upscaling will be disabled");
        m_upscaler.reset();
    }
    
    Logger::Info("Overlay renderer initialized");
    return true;
}

void OverlayRenderer::Shutdown()
{
    if (m_upscaler)
    {
        m_upscaler->Shutdown();
        m_upscaler.reset();
    }
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
    
    // Check for tearing support
    BOOL allowTearing = FALSE;
    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory.As(&factory5)))
    {
        factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
    }
    m_tearingSupported = (allowTearing == TRUE);
    
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.Flags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    
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
        m_tearingSupported = false;
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
    
    Logger::Info("Overlay swap chain created (tearing: %s)", m_tearingSupported ? "supported" : "not supported");
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
    
    // If no captured frame, just clear and return
    if (!capturedFrame)
    {
        if (m_renderTargetView)
        {
            float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            m_context->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);
        }
        return;
    }
    
    // Get captured frame dimensions
    D3D11_TEXTURE2D_DESC srcDesc;
    capturedFrame->GetDesc(&srcDesc);
    
    // Get back buffer dimensions  
    D3D11_TEXTURE2D_DESC dstDesc;
    m_backBuffer->GetDesc(&dstDesc);
    
    // Determine the source texture to copy (either upscaled or original)
    ID3D11Texture2D* sourceTexture = capturedFrame;
    
    // Apply upscaling only if enabled AND factor > 1
    if (m_upscaleEnabled && m_upscaler && m_upscaleFactor > 1.01f)
    {
        // Calculate upscaled dimensions
        uint32_t upscaledWidth = static_cast<uint32_t>(srcDesc.Width * m_upscaleFactor);
        uint32_t upscaledHeight = static_cast<uint32_t>(srcDesc.Height * m_upscaleFactor);
        
        // Clamp to back buffer size
        upscaledWidth = min(upscaledWidth, dstDesc.Width);
        upscaledHeight = min(upscaledHeight, dstDesc.Height);
        
        // Only upscale if dimensions actually change
        if (upscaledWidth > srcDesc.Width || upscaledHeight > srcDesc.Height)
        {
            ID3D11Texture2D* upscaledTexture = m_upscaler->Upscale(
                capturedFrame,
                upscaledWidth,
                upscaledHeight,
                m_upscaleMethod
            );
            
            if (upscaledTexture)
            {
                sourceTexture = upscaledTexture;
                sourceTexture->GetDesc(&srcDesc);
            }
        }
    }
    
    // Copy the source texture to back buffer
    if (srcDesc.Width == dstDesc.Width && srcDesc.Height == dstDesc.Height)
    {
        // Sizes match - direct copy (fastest path)
        m_context->CopyResource(m_backBuffer.Get(), sourceTexture);
    }
    else
    {
        // Sizes differ - copy region
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
            sourceTexture, 0,
            &srcBox
        );
    }
    // Note: No Flush() here - Present() will synchronize
}

void OverlayRenderer::Present(bool vsync)
{
    if (m_swapChain)
    {
        // Use DXGI_PRESENT_ALLOW_TEARING for lowest latency when not vsync and tearing is supported
        UINT flags = (!vsync && m_tearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;
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
    
    UINT flags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    HRESULT hr = m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, flags);
    if (FAILED(hr))
    {
        hr = m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    }
    
    if (SUCCEEDED(hr))
    {
        CreateRenderTarget();
    }
}

void OverlayRenderer::SetSharpness(float sharpness)
{
    if (m_upscaler)
    {
        m_upscaler->SetSharpness(sharpness);
    }
}

float OverlayRenderer::GetSharpness() const
{
    if (m_upscaler)
    {
        return m_upscaler->GetSharpness();
    }
    return 0.5f;
}
