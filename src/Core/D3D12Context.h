#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>

using Microsoft::WRL::ComPtr;

class D3D12Context
{
public:
    static const uint32_t FRAME_COUNT = 2;

    D3D12Context();
    ~D3D12Context();

    bool Initialize(HWND hwnd, uint32_t width, uint32_t height);
    void Shutdown();
    void Resize(uint32_t width, uint32_t height);

    void BeginFrame();
    void EndFrame();
    void WaitForGPU();

    // Getters
    ID3D12Device* GetDevice() const { return m_device.Get(); }
    ID3D12CommandQueue* GetCommandQueue() const { return m_commandQueue.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const { return m_commandList.Get(); }
    ID3D12Resource* GetCurrentBackBuffer() const { return m_renderTargets[m_frameIndex].Get(); }
    uint32_t GetCurrentFrameIndex() const { return m_frameIndex; }
    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }
    
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTVHandle() const {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += m_frameIndex * m_rtvDescriptorSize;
        return handle;
    }

    // Resource creation helpers
    ID3D12Resource* CreateTexture2D(uint32_t width, uint32_t height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags);
    ID3D12Resource* CreateBuffer(uint64_t size, D3D12_HEAP_TYPE heapType);

private:
    bool CreateDevice();
    bool CreateCommandQueue();
    bool CreateSwapChain(HWND hwnd);
    bool CreateRenderTargets();
    bool CreateCommandAllocators();
    bool CreateCommandList();
    bool CreateFence();

private:
    ComPtr<IDXGIFactory4> m_factory;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<IDXGISwapChain3> m_swapChain;

    ComPtr<ID3D12Resource> m_renderTargets[FRAME_COUNT];
    ComPtr<ID3D12CommandAllocator> m_commandAllocators[FRAME_COUNT];
    ComPtr<ID3D12GraphicsCommandList> m_commandList;

    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    uint32_t m_rtvDescriptorSize = 0;

    ComPtr<ID3D12Fence> m_fence;
    uint64_t m_fenceValues[FRAME_COUNT] = {};
    HANDLE m_fenceEvent = nullptr;

    uint32_t m_frameIndex = 0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    HWND m_hwnd = nullptr;
};