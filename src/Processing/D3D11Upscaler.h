#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>

using Microsoft::WRL::ComPtr;

enum class UpscaleMethod
{
    Bilinear,
    FSR  // FidelityFX Super Resolution inspired
};

// D3D11-based upscaler for the overlay system
// Supports bilinear and FSR-style edge-adaptive upscaling
class D3D11Upscaler
{
public:
    D3D11Upscaler();
    ~D3D11Upscaler();

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    // Upscale the input texture to the specified output size
    // Returns the upscaled texture (owned by this class)
    ID3D11Texture2D* Upscale(
        ID3D11Texture2D* inputTexture,
        uint32_t outputWidth,
        uint32_t outputHeight,
        UpscaleMethod method = UpscaleMethod::FSR
    );

    // Get the upscaled texture directly
    ID3D11Texture2D* GetOutputTexture() const { return m_outputTexture.Get(); }

    // Set sharpness for FSR (0.0 = smooth, 1.0 = sharp)
    void SetSharpness(float sharpness) { m_sharpness = sharpness; }
    float GetSharpness() const { return m_sharpness; }

private:
    bool CreateComputeShaders();
    bool CreateConstantBuffer();
    bool EnsureOutputTexture(uint32_t width, uint32_t height, DXGI_FORMAT format);
    bool LoadCompiledShader(const std::wstring& filename, ComPtr<ID3D11ComputeShader>& shader);
    bool CompileShaderFromSource(const char* source, const char* entryPoint, ComPtr<ID3D11ComputeShader>& shader);

private:
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Compute shaders
    ComPtr<ID3D11ComputeShader> m_bilinearShader;
    ComPtr<ID3D11ComputeShader> m_fsrShader;

    // Output texture and UAV
    ComPtr<ID3D11Texture2D> m_outputTexture;
    ComPtr<ID3D11UnorderedAccessView> m_outputUAV;

    // Constant buffer
    ComPtr<ID3D11Buffer> m_constantBuffer;

    // Sampler state for texture sampling
    ComPtr<ID3D11SamplerState> m_linearSampler;

    // Current output dimensions
    uint32_t m_outputWidth = 0;
    uint32_t m_outputHeight = 0;
    DXGI_FORMAT m_outputFormat = DXGI_FORMAT_UNKNOWN;

    // Cached input SRV to avoid recreation every frame
    ComPtr<ID3D11ShaderResourceView> m_cachedInputSRV;
    ID3D11Texture2D* m_cachedInputTexture = nullptr;

    // Settings
    float m_sharpness = 0.5f;

    // Shader constant structure (must match HLSL)
    struct UpscaleConstants
    {
        float inputWidth;
        float inputHeight;
        float outputWidth;
        float outputHeight;
        float sharpness;
        float padding[3];  // Align to 16 bytes
    };
};
