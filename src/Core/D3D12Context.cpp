#include "D3D12Context.h"
#include "../Utils/Logger.h"
#include <stdexcept>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

D3D12Context::D3D12Context()
{
}

D3D12Context::~D3D12Context()
{
    Shutdown();
}

bool D3D12Context::Initialize(HWND hwnd, uint32_t width, uint32_t height)
{
    m_hwnd = hwnd;
    m_width = width;
    m_height = height;

    if (!CreateDevice()) return false;
    if (!CreateCommandQueue()) return false;
    if (!CreateSwapChain(hwnd)) return false;
    if (!CreateRenderTargets()) return false;
    if (!CreateCommandAllocators()) return false;
    if (!CreateCommandList()) return false;
    if (!CreateFence()) return false;

    Logger::Info("D3D12 context initialized");
    return true;
}

void D3D12Context::Shutdown()
{
    WaitForGPU();

    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }

    Logger::Info("D3D12 context shut down");
}

void D3D12Context::Resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) return;

    WaitForGPU();

    // Release render targets
    for (auto& rt : m_renderTargets)
    {
        rt.Reset();
    }

    // Resize swap chain
    HRESULT hr = m_swapChain->ResizeBuffers(
        FRAME_COUNT,
        width,
        height,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        0
    );

    if (FAILED(hr))
    {
        Logger::Error("Failed to resize swap chain");
        return;
    }

    m_width = width;
    m_height = height;
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    CreateRenderTargets();
    Logger::Info("Resized to %ux%u", width, height);
}

void D3D12Context::BeginFrame()
{
    // Only wait if this frame buffer has been used before
    // m_fenceValues[i] starts at 0, gets incremented to 1 after first use
    if (m_fenceValues[m_frameIndex] > 0)
    {
        // Wait for GPU to finish with this frame's command allocator
        if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
        {
            HRESULT hr = m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent);
            if (FAILED(hr))
            {
                Logger::Error("SetEventOnCompletion failed");
                return;
            }
            DWORD result = WaitForSingleObject(m_fenceEvent, 5000);
            if (result != WAIT_OBJECT_0)
            {
                Logger::Error("Wait for fence timed out (value=%llu, completed=%llu)", 
                    m_fenceValues[m_frameIndex], m_fence->GetCompletedValue());
                return;
            }
        }
    }

    m_commandAllocators[m_frameIndex]->Reset();
    m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr);
}

void D3D12Context::EndFrame()
{
    // Transition backbuffer to present state
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    m_commandList->ResourceBarrier(1, &barrier);

    m_commandList->Close();

    ID3D12CommandList* cmdLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, cmdLists);

    // Present
    m_swapChain->Present(1, 0);

    // Schedule a signal for the current frame
    m_fenceValues[m_frameIndex]++;
    m_commandQueue->Signal(m_fence.Get(), m_fenceValues[m_frameIndex]);

    // Update frame index
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void D3D12Context::WaitForGPU()
{
    const uint64_t fenceValue = m_fenceValues[m_frameIndex];
    m_commandQueue->Signal(m_fence.Get(), fenceValue);
    m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent);
    WaitForSingleObject(m_fenceEvent, INFINITE);
    m_fenceValues[m_frameIndex]++;
}

bool D3D12Context::CreateDevice()
{
#ifdef _DEBUG
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
        Logger::Info("D3D12 debug layer enabled");
    }
#endif

    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_factory));
    if (FAILED(hr))
    {
        Logger::Error("Failed to create DXGI factory");
        return false;
    }

    ComPtr<IDXGIAdapter1> adapter;
    hr = m_factory->EnumAdapters1(0, &adapter);
    if (FAILED(hr))
    {
        Logger::Error("Failed to enumerate adapters");
        return false;
    }

    hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device));
    if (FAILED(hr))
    {
        Logger::Error("Failed to create D3D12 device");
        return false;
    }

    Logger::Info("D3D12 device created");
    return true;
}

bool D3D12Context::CreateCommandQueue()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    HRESULT hr = m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue));
    if (FAILED(hr))
    {
        Logger::Error("Failed to create command queue");
        return false;
    }

    return true;
}

bool D3D12Context::CreateSwapChain(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = FRAME_COUNT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> swapChain1;
    HRESULT hr = m_factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),
        hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain1
    );

    if (FAILED(hr))
    {
        Logger::Error("Failed to create swap chain");
        return false;
    }

    hr = swapChain1.As(&m_swapChain);
    if (FAILED(hr))
    {
        Logger::Error("Failed to get IDXGISwapChain3 interface");
        return false;
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    return true;
}

bool D3D12Context::CreateRenderTargets()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FRAME_COUNT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    HRESULT hr = m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap));
    if (FAILED(hr))
    {
        Logger::Error("Failed to create RTV descriptor heap");
        return false;
    }

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t i = 0; i < FRAME_COUNT; i++)
    {
        hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
        if (FAILED(hr))
        {
            Logger::Error("Failed to get swap chain buffer %u", i);
            return false;
        }

        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += m_rtvDescriptorSize;
    }

    return true;
}

bool D3D12Context::CreateCommandAllocators()
{
    for (uint32_t i = 0; i < FRAME_COUNT; i++)
    {
        HRESULT hr = m_device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&m_commandAllocators[i])
        );

        if (FAILED(hr))
        {
            Logger::Error("Failed to create command allocator %u", i);
            return false;
        }
    }

    return true;
}

bool D3D12Context::CreateCommandList()
{
    HRESULT hr = m_device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_commandAllocators[0].Get(),
        nullptr,
        IID_PPV_ARGS(&m_commandList)
    );

    if (FAILED(hr))
    {
        Logger::Error("Failed to create command list");
        return false;
    }

    m_commandList->Close();
    return true;
}

bool D3D12Context::CreateFence()
{
    HRESULT hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr))
    {
        Logger::Error("Failed to create fence");
        return false;
    }

    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent)
    {
        Logger::Error("Failed to create fence event");
        return false;
    }

    for (uint32_t i = 0; i < FRAME_COUNT; i++)
    {
        m_fenceValues[i] = 0;
    }

    return true;
}

ID3D12Resource* D3D12Context::CreateTexture2D(uint32_t width, uint32_t height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    ID3D12Resource* resource = nullptr;
    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&resource)
    );

    if (FAILED(hr))
    {
        Logger::Error("Failed to create texture 2D (%dx%d, format %d): HRESULT 0x%08X", width, height, format, hr);
        return nullptr;
    }

    return resource;
}

ID3D12Resource* D3D12Context::CreateBuffer(uint64_t size, D3D12_HEAP_TYPE heapType)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = heapType;

    ID3D12Resource* resource = nullptr;
    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&resource)
    );

    if (FAILED(hr))
    {
        Logger::Error("Failed to create buffer");
        return nullptr;
    }

    return resource;
}