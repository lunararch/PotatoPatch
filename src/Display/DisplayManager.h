#pragma once
#include "../Core/D3D12Context.h"

class DisplayManager
{
public:
    bool Initialize(D3D12Context* context);
    void Shutdown();
    void RenderToBackbuffer(ID3D12Resource* sourceTexture, ID3D12Resource* backbuffer);

private:
    D3D12Context* m_context = nullptr;
};