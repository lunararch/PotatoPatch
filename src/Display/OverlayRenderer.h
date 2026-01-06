#pragma once
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdint>

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
    void RenderFrame(ID3D11Texture2D* capturedFrame);
    
    // Present the frame
    void Present(bool vsync = false);
    
    // Handle resize
    void Resize(uint32_t width, uint32_t height);

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
    
    uint32_t m_width = 0;
    uint32_t m_height = 0;
};
