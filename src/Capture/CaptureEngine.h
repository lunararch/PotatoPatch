#pragma once
#include "../Core/D3D12Context.h"
#include <Windows.h>

class CaptureEngine
{
public:
    CaptureEngine();
    ~CaptureEngine();

    bool Initialize(D3D12Context* context);
    void Shutdown();

    ID3D12Resource* CaptureWindow(HWND targetWindow);

private:
    D3D12Context* m_context = nullptr;
    ID3D12Resource* m_captureTexture = nullptr;

    uint32_t m_captureWidth = 0;
    uint32_t m_captureHeight = 0;
};