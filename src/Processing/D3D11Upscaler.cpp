#include "D3D11Upscaler.h"
#include "../Utils/Logger.h"
#include <d3dcompiler.h>
#include <fstream>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

// Embedded shader source for bilinear upscaling
static const char* s_bilinearShaderSource = R"(
Texture2D<float4> InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);
SamplerState LinearSampler : register(s0);

cbuffer Constants : register(b0)
{
    float inputWidth;
    float inputHeight;
    float outputWidth;
    float outputHeight;
    float sharpness;
    float3 padding;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 outputPos = dispatchThreadID.xy;

    if (outputPos.x >= (uint)outputWidth || outputPos.y >= (uint)outputHeight)
        return;

    // Calculate UV coordinates
    float2 uv = (float2(outputPos) + 0.5f) / float2(outputWidth, outputHeight);

    // Sample with bilinear filtering
    float4 color = InputTexture.SampleLevel(LinearSampler, uv, 0);

    OutputTexture[outputPos] = color;
}
)";

// Embedded shader source for FSR-style edge-adaptive upscaling
static const char* s_fsrShaderSource = R"(
Texture2D<float4> InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);
SamplerState LinearSampler : register(s0);

cbuffer Constants : register(b0)
{
    float inputWidth;
    float inputHeight;
    float outputWidth;
    float outputHeight;
    float sharpness;
    float3 padding;
};

// Calculate luminance for edge detection
float GetLuminance(float3 color)
{
    return dot(color, float3(0.299f, 0.587f, 0.114f));
}

// FSR-inspired Robust Contrast Adaptive Sharpening (RCAS)
float4 FSRUpscale(float2 uv)
{
    float2 inputSize = float2(inputWidth, inputHeight);
    float2 texelSize = 1.0f / inputSize;
    
    // Get the center sample
    float4 center = InputTexture.SampleLevel(LinearSampler, uv, 0);
    
    // Sample cross neighborhood for edge detection
    float4 north = InputTexture.SampleLevel(LinearSampler, uv + float2(0, -texelSize.y), 0);
    float4 south = InputTexture.SampleLevel(LinearSampler, uv + float2(0, texelSize.y), 0);
    float4 east = InputTexture.SampleLevel(LinearSampler, uv + float2(texelSize.x, 0), 0);
    float4 west = InputTexture.SampleLevel(LinearSampler, uv + float2(-texelSize.x, 0), 0);
    
    // Calculate luminance values
    float lumCenter = GetLuminance(center.rgb);
    float lumNorth = GetLuminance(north.rgb);
    float lumSouth = GetLuminance(south.rgb);
    float lumEast = GetLuminance(east.rgb);
    float lumWest = GetLuminance(west.rgb);
    
    // Find min and max luminance in cross pattern
    float lumMin = min(lumCenter, min(min(lumNorth, lumSouth), min(lumEast, lumWest)));
    float lumMax = max(lumCenter, max(max(lumNorth, lumSouth), max(lumEast, lumWest)));
    
    // Calculate local contrast
    float lumRange = lumMax - lumMin;
    float lumAvg = (lumNorth + lumSouth + lumEast + lumWest) * 0.25f;
    
    // Adaptive sharpening strength based on contrast
    float edgeStrength = saturate(lumRange * 4.0f);
    float sharpenAmount = sharpness * edgeStrength;
    
    // Apply sharpening: enhance center relative to neighbors
    float4 neighbors = (north + south + east + west) * 0.25f;
    float4 sharpened = center + (center - neighbors) * sharpenAmount;
    
    // Clamp to prevent ringing artifacts
    float4 minColor = min(center, min(min(north, south), min(east, west)));
    float4 maxColor = max(center, max(max(north, south), max(east, west)));
    sharpened = clamp(sharpened, minColor, maxColor);
    
    return sharpened;
}

// Enhanced upscaling with edge-aware interpolation
float4 FSREdgeAware(float2 uv)
{
    float2 inputSize = float2(inputWidth, inputHeight);
    float2 outputSize = float2(outputWidth, outputHeight);
    float2 texelSize = 1.0f / inputSize;
    
    // Calculate position in input texture space
    float2 inputPos = uv * inputSize - 0.5f;
    float2 inputPosFloor = floor(inputPos);
    float2 f = inputPos - inputPosFloor;
    
    // Sample 4x4 neighborhood for bicubic-like quality
    float4 samples[16];
    float weights[16];
    float totalWeight = 0.0f;
    
    [unroll]
    for (int y = -1; y <= 2; y++)
    {
        [unroll]
        for (int x = -1; x <= 2; x++)
        {
            int idx = (y + 1) * 4 + (x + 1);
            float2 samplePos = (inputPosFloor + float2(x, y) + 0.5f) / inputSize;
            samplePos = saturate(samplePos);
            samples[idx] = InputTexture.SampleLevel(LinearSampler, samplePos, 0);
            
            // Mitchell-Netravali-like filter weights
            float2 d = abs(float2(x, y) - f);
            float wx = (d.x < 1.0f) ? (1.0f - 2.0f * d.x * d.x + d.x * d.x * d.x) :
                       (d.x < 2.0f) ? (4.0f - 8.0f * d.x + 5.0f * d.x * d.x - d.x * d.x * d.x) : 0.0f;
            float wy = (d.y < 1.0f) ? (1.0f - 2.0f * d.y * d.y + d.y * d.y * d.y) :
                       (d.y < 2.0f) ? (4.0f - 8.0f * d.y + 5.0f * d.y * d.y - d.y * d.y * d.y) : 0.0f;
            weights[idx] = max(0.0f, wx * wy);
            totalWeight += weights[idx];
        }
    }
    
    // Normalize and accumulate
    float4 result = float4(0, 0, 0, 0);
    [unroll]
    for (int i = 0; i < 16; i++)
    {
        result += samples[i] * (weights[i] / max(totalWeight, 0.0001f));
    }
    
    // Apply FSR-style sharpening on top
    float2 sharpTexelSize = 1.0f / outputSize;
    float4 sharpNorth = InputTexture.SampleLevel(LinearSampler, uv + float2(0, -sharpTexelSize.y * 0.5f), 0);
    float4 sharpSouth = InputTexture.SampleLevel(LinearSampler, uv + float2(0, sharpTexelSize.y * 0.5f), 0);
    float4 sharpEast = InputTexture.SampleLevel(LinearSampler, uv + float2(sharpTexelSize.x * 0.5f, 0), 0);
    float4 sharpWest = InputTexture.SampleLevel(LinearSampler, uv + float2(-sharpTexelSize.x * 0.5f, 0), 0);
    
    float lumCenter = GetLuminance(result.rgb);
    float lumNeighbors = (GetLuminance(sharpNorth.rgb) + GetLuminance(sharpSouth.rgb) +
                         GetLuminance(sharpEast.rgb) + GetLuminance(sharpWest.rgb)) * 0.25f;
    float edgeStrength = saturate(abs(lumCenter - lumNeighbors) * 8.0f);
    
    float4 neighbors = (sharpNorth + sharpSouth + sharpEast + sharpWest) * 0.25f;
    result = result + (result - neighbors) * sharpness * edgeStrength * 0.5f;
    
    return saturate(result);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 outputPos = dispatchThreadID.xy;

    if (outputPos.x >= (uint)outputWidth || outputPos.y >= (uint)outputHeight)
        return;

    float2 uv = (float2(outputPos) + 0.5f) / float2(outputWidth, outputHeight);
    
    // Use edge-aware upscaling
    float4 color = FSRUpscale(uv);
    
    OutputTexture[outputPos] = color;
}
)";

D3D11Upscaler::D3D11Upscaler()
{
}

D3D11Upscaler::~D3D11Upscaler()
{
    Shutdown();
}

bool D3D11Upscaler::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (!device || !context)
    {
        Logger::Error("D3D11Upscaler: Invalid device or context");
        return false;
    }

    m_device = device;
    m_context = context;

    if (!CreateComputeShaders())
    {
        Logger::Error("D3D11Upscaler: Failed to create compute shaders");
        return false;
    }

    if (!CreateConstantBuffer())
    {
        Logger::Error("D3D11Upscaler: Failed to create constant buffer");
        return false;
    }

    // Create linear sampler
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    HRESULT hr = m_device->CreateSamplerState(&samplerDesc, &m_linearSampler);
    if (FAILED(hr))
    {
        Logger::Error("D3D11Upscaler: Failed to create sampler state: 0x%08X", hr);
        return false;
    }

    Logger::Info("D3D11Upscaler initialized successfully");
    return true;
}

void D3D11Upscaler::Shutdown()
{
    m_bilinearShader.Reset();
    m_fsrShader.Reset();
    m_outputTexture.Reset();
    m_outputUAV.Reset();
    m_constantBuffer.Reset();
    m_linearSampler.Reset();
    
    m_device = nullptr;
    m_context = nullptr;
    m_outputWidth = 0;
    m_outputHeight = 0;
}

bool D3D11Upscaler::CreateComputeShaders()
{
    // Try to load pre-compiled shaders first, fall back to runtime compilation
    
    // Compile bilinear shader
    if (!CompileShaderFromSource(s_bilinearShaderSource, "CSMain", m_bilinearShader))
    {
        Logger::Error("D3D11Upscaler: Failed to compile bilinear shader");
        return false;
    }

    // Compile FSR shader
    if (!CompileShaderFromSource(s_fsrShaderSource, "CSMain", m_fsrShader))
    {
        Logger::Error("D3D11Upscaler: Failed to compile FSR shader");
        return false;
    }

    Logger::Info("D3D11Upscaler: Compute shaders compiled successfully");
    return true;
}

bool D3D11Upscaler::CompileShaderFromSource(const char* source, const char* entryPoint, ComPtr<ID3D11ComputeShader>& shader)
{
    ComPtr<ID3DBlob> shaderBlob;
    ComPtr<ID3DBlob> errorBlob;

    UINT compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompile(
        source,
        strlen(source),
        nullptr,
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint,
        "cs_5_0",
        compileFlags,
        0,
        &shaderBlob,
        &errorBlob
    );

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            Logger::Error("Shader compilation error: %s", (char*)errorBlob->GetBufferPointer());
        }
        return false;
    }

    hr = m_device->CreateComputeShader(
        shaderBlob->GetBufferPointer(),
        shaderBlob->GetBufferSize(),
        nullptr,
        &shader
    );

    if (FAILED(hr))
    {
        Logger::Error("Failed to create compute shader: 0x%08X", hr);
        return false;
    }

    return true;
}

bool D3D11Upscaler::LoadCompiledShader(const std::wstring& filename, ComPtr<ID3D11ComputeShader>& shader)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open())
    {
        return false;
    }

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> bytecode(size);
    file.read(bytecode.data(), size);
    file.close();

    HRESULT hr = m_device->CreateComputeShader(
        bytecode.data(),
        bytecode.size(),
        nullptr,
        &shader
    );

    return SUCCEEDED(hr);
}

bool D3D11Upscaler::CreateConstantBuffer()
{
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(UpscaleConstants);
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = m_device->CreateBuffer(&bufferDesc, nullptr, &m_constantBuffer);
    if (FAILED(hr))
    {
        Logger::Error("D3D11Upscaler: Failed to create constant buffer: 0x%08X", hr);
        return false;
    }

    return true;
}

bool D3D11Upscaler::EnsureOutputTexture(uint32_t width, uint32_t height, DXGI_FORMAT format)
{
    // Check if we need to recreate the output texture
    if (m_outputTexture && m_outputWidth == width && m_outputHeight == height && m_outputFormat == format)
    {
        return true;
    }

    // Release old resources
    m_outputUAV.Reset();
    m_outputTexture.Reset();

    // Create new output texture
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, &m_outputTexture);
    if (FAILED(hr))
    {
        Logger::Error("D3D11Upscaler: Failed to create output texture: 0x%08X", hr);
        return false;
    }

    // Create UAV
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = format;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;

    hr = m_device->CreateUnorderedAccessView(m_outputTexture.Get(), &uavDesc, &m_outputUAV);
    if (FAILED(hr))
    {
        Logger::Error("D3D11Upscaler: Failed to create UAV: 0x%08X", hr);
        return false;
    }

    m_outputWidth = width;
    m_outputHeight = height;
    m_outputFormat = format;

    Logger::Info("D3D11Upscaler: Created output texture %ux%u", width, height);
    return true;
}

ID3D11Texture2D* D3D11Upscaler::Upscale(
    ID3D11Texture2D* inputTexture,
    uint32_t outputWidth,
    uint32_t outputHeight,
    UpscaleMethod method)
{
    if (!inputTexture || !m_device || !m_context)
    {
        return nullptr;
    }

    // Get input texture description
    D3D11_TEXTURE2D_DESC inputDesc;
    inputTexture->GetDesc(&inputDesc);

    // If output size matches input, just return input (no upscaling needed)
    if (inputDesc.Width == outputWidth && inputDesc.Height == outputHeight)
    {
        return inputTexture;
    }

    // Ensure output texture exists
    if (!EnsureOutputTexture(outputWidth, outputHeight, inputDesc.Format))
    {
        return nullptr;
    }

    // Only recreate SRV if input texture changed
    if (m_cachedInputTexture != inputTexture)
    {
        m_cachedInputSRV.Reset();
        
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = inputDesc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;

        HRESULT hr = m_device->CreateShaderResourceView(inputTexture, &srvDesc, &m_cachedInputSRV);
        if (FAILED(hr))
        {
            Logger::Error("D3D11Upscaler: Failed to create input SRV: 0x%08X", hr);
            return nullptr;
        }
        m_cachedInputTexture = inputTexture;
    }

    // Update constant buffer
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = m_context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (SUCCEEDED(hr))
    {
        UpscaleConstants* constants = static_cast<UpscaleConstants*>(mappedResource.pData);
        constants->inputWidth = static_cast<float>(inputDesc.Width);
        constants->inputHeight = static_cast<float>(inputDesc.Height);
        constants->outputWidth = static_cast<float>(outputWidth);
        constants->outputHeight = static_cast<float>(outputHeight);
        constants->sharpness = m_sharpness;
        m_context->Unmap(m_constantBuffer.Get(), 0);
    }

    // Select shader
    ID3D11ComputeShader* shader = (method == UpscaleMethod::FSR) ? m_fsrShader.Get() : m_bilinearShader.Get();

    // Set compute shader state
    m_context->CSSetShader(shader, nullptr, 0);
    m_context->CSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    m_context->CSSetShaderResources(0, 1, m_cachedInputSRV.GetAddressOf());
    m_context->CSSetUnorderedAccessViews(0, 1, m_outputUAV.GetAddressOf(), nullptr);
    m_context->CSSetSamplers(0, 1, m_linearSampler.GetAddressOf());

    // Dispatch compute shader
    uint32_t threadGroupsX = (outputWidth + 7) / 8;
    uint32_t threadGroupsY = (outputHeight + 7) / 8;
    m_context->Dispatch(threadGroupsX, threadGroupsY, 1);

    // Clear shader state
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    m_context->CSSetShaderResources(0, 1, &nullSRV);
    m_context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    m_context->CSSetShader(nullptr, nullptr, 0);

    return m_outputTexture.Get();
}
