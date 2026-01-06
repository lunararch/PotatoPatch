#include "Upscaler.h"
#include "../Utils/Logger.h"
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

Upscaler::Upscaler()
{
}

Upscaler::~Upscaler()
{
    Shutdown();
}

bool Upscaler::Initialize(D3D12Context* context)
{
    m_context = context;

    if (!CreatePipelineState())
    {
        Logger::Error("Failed to create upscaler pipeline state");
        return false;
    }

    Logger::Info("Upscaler initialized");
    return true;
}

void Upscaler::Shutdown()
{
    if (m_outputTexture) m_outputTexture->Release();
    if (m_pipelineState) m_pipelineState->Release();
    if (m_rootSignature) m_rootSignature->Release();
    if (m_srvHeap) m_srvHeap->Release();
    if (m_uavHeap) m_uavHeap->Release();
}

bool Upscaler::CreatePipelineState()
{
    // Create root signature
    D3D12_ROOT_PARAMETER rootParams[3];

    // SRV (input texture)
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;

    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // UAV (output texture)
    D3D12_DESCRIPTOR_RANGE uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &uavRange;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Constants
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[2].Constants.ShaderRegister = 0;
    rootParams[2].Constants.Num32BitValues = 4;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.ShaderRegister = 0;
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 3;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &samplerDesc;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob* signature = nullptr;
    ID3DBlob* error = nullptr;

    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if (FAILED(hr))
    {
        if (error)
        {
            Logger::Error("Root signature serialization error: %s", (char*)error->GetBufferPointer());
            error->Release();
        }
        return false;
    }

    hr = m_context->GetDevice()->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSignature)
    );

    signature->Release();

    if (FAILED(hr))
    {
        Logger::Error("Failed to create root signature");
        return false;
    }

    // Create descriptor heaps
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 1;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    m_context->GetDevice()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvHeap));
    m_context->GetDevice()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_uavHeap));

    Logger::Info("Upscaler pipeline state created");
    return true;
}

ID3D12Resource* Upscaler::Upscale(ID3D12Resource* inputTexture, float scale)
{
    if (!inputTexture)
    {
        return nullptr;
    }

    D3D12_RESOURCE_DESC inputDesc = inputTexture->GetDesc();
    uint32_t outputWidth = static_cast<uint32_t>(inputDesc.Width * scale);
    uint32_t outputHeight = static_cast<uint32_t>(inputDesc.Height * scale);

    // Create or resize output texture
    if (!m_outputTexture || m_outputWidth != outputWidth || m_outputHeight != outputHeight)
    {
        if (m_outputTexture)
        {
            m_outputTexture->Release();
        }

        m_outputTexture = m_context->CreateTexture2D(
            outputWidth, outputHeight,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
        );

        m_outputWidth = outputWidth;
        m_outputHeight = outputHeight;
    }

    // In a real implementation, you would:
    // 1. Set root signature and pipeline state
    // 2. Create SRV for input texture
    // 3. Create UAV for output texture
    // 4. Set constants (input/output dimensions)
    // 5. Dispatch compute shader

    // For this minimal example, we're just returning the output texture
    // which would be filled by the compute shader

    return m_outputTexture;
}