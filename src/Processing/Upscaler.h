#pragma once
#include "../Core/D3D12Context.h"

class Upscaler
{
public:
    Upscaler();
    ~Upscaler();

    bool Initialize(D3D12Context* context);
    void Shutdown();

    ID3D12Resource* Upscale(ID3D12Resource* inputTexture, float scale);

private:
    bool LoadShaders();
    bool CreatePipelineState();

private:
    D3D12Context* m_context = nullptr;

    ID3D12RootSignature* m_rootSignature = nullptr;
    ID3D12PipelineState* m_pipelineState = nullptr;

    ID3D12Resource* m_outputTexture = nullptr;
    ID3D12DescriptorHeap* m_srvHeap = nullptr;
    ID3D12DescriptorHeap* m_uavHeap = nullptr;

    uint32_t m_outputWidth = 0;
    uint32_t m_outputHeight = 0;
};