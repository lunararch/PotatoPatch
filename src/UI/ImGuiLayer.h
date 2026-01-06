#pragma once
#include "../Core/D3D12Context.h"
#include <Windows.h>

class ImGuiLayer
{
public:
    bool Initialize(HWND hwnd, D3D12Context* context);
    void Shutdown();
    void BeginFrame();
    void EndFrame();
    void Render(ID3D12Resource* renderTarget);

private:
    D3D12Context* m_context = nullptr;
    ID3D12DescriptorHeap* m_srvHeap = nullptr;
};