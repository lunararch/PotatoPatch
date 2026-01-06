#pragma once
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdint>
#include <memory>
#include "../Processing/D3D11Upscaler.h"

using Microsoft::WRL::ComPtr;

// Handles rendering captured D3D11 frames to a D3D11 swap chain
// This avoids the complex D3D11/D3D12 interop by using D3D11 for everything in the overlay
class OverlayRenderer
{
public:
    OverlayRenderer();
    ~OverlayRenderer();

    bool Initialize(HWND overlayWindow, ID3D11Device* captureDevice, ID3D11DeviceContext* captureContext);
    void Shutdown();

    // Render a captured frame to the overlay window
    // If upscaling is enabled, the frame will be upscaled to the output size
    void RenderFrame(ID3D11Texture2D* capturedFrame);
    
    // Present the frame
    void Present(bool vsync = false);
    
    // Handle resize
    void Resize(uint32_t width, uint32_t height);

    // Upscaling settings
    void SetUpscalingEnabled(bool enabled) { m_upscaleEnabled = enabled; }
    bool IsUpscalingEnabled() const { return m_upscaleEnabled; }
    
    void SetUpscaleMethod(UpscaleMethod method) { m_upscaleMethod = method; }
    UpscaleMethod GetUpscaleMethod() const { return m_upscaleMethod; }
    
    void SetUpscaleFactor(float factor) { m_upscaleFactor = factor; }
    float GetUpscaleFactor() const { return m_upscaleFactor; }
    
    void SetSharpness(float sharpness);
    float GetSharpness() const;

private:
    bool CreateSwapChain(HWND hwnd);
    bool CreateRenderTarget();
    void ReleaseRenderTarget();

private:
    ID3D11Device* m_device = nullptr;  // Shared with capture
    ID3D11DeviceContext* m_context = nullptr;  // Shared with capture
    
    ComPtr<IDXGISwapChain1> m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_renderTargetView;
    ComPtr<ID3D11Texture2D> m_backBuffer;
    
    // Upscaler
    std::unique_ptr<D3D11Upscaler> m_upscaler;
    bool m_upscaleEnabled = false;
    UpscaleMethod m_upscaleMethod = UpscaleMethod::FSR;
    float m_upscaleFactor = 1.5f;
    
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_tearingSupported = false;
};
