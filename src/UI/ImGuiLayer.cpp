#include "ImGuiLayer.h"
#include "../Utils/Logger.h"
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx12.h>

bool ImGuiLayer::Initialize(HWND hwnd, D3D12Context* context)
{
    m_context = context;

    // Create descriptor heap for ImGui
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    if (FAILED(context->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_srvHeap))))
    {
        Logger::Error("Failed to create ImGui descriptor heap");
        return false;
    }

    // Setup ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    // Build font atlas
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX12_Init(
        context->GetDevice(),
        D3D12Context::FRAME_COUNT,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        m_srvHeap,
        m_srvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_srvHeap->GetGPUDescriptorHandleForHeapStart()
    );

    Logger::Info("ImGui layer initialized");
    return true;
}

void ImGuiLayer::Shutdown()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (m_srvHeap)
    {
        m_srvHeap->Release();
        m_srvHeap = nullptr;
    }
}

void ImGuiLayer::BeginFrame()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::EndFrame()
{
    ImGui::Render();
}

void ImGuiLayer::Render(ID3D12Resource* renderTarget)
{
    auto cmdList = m_context->GetCommandList();

    // Set descriptor heaps for ImGui
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap };
    cmdList->SetDescriptorHeaps(1, heaps);

    // Set render target
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_context->GetCurrentRTVHandle();
    cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Set viewport and scissor rect
    D3D12_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(m_context->GetWidth());
    viewport.Height = static_cast<float>(m_context->GetHeight());
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    cmdList->RSSetViewports(1, &viewport);

    D3D12_RECT scissorRect = {};
    scissorRect.right = static_cast<LONG>(m_context->GetWidth());
    scissorRect.bottom = static_cast<LONG>(m_context->GetHeight());
    cmdList->RSSetScissorRects(1, &scissorRect);

    // Render ImGui
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
}