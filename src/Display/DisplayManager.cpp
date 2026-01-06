#include "DisplayManager.h"
#include "../Utils/Logger.h"

bool DisplayManager::Initialize(D3D12Context* context)
{
    m_context = context;
    Logger::Info("Display manager initialized");
    return true;
}

void DisplayManager::Shutdown()
{
}

void DisplayManager::RenderToBackbuffer(ID3D12Resource* sourceTexture, ID3D12Resource* backbuffer)
{
    auto cmdList = m_context->GetCommandList();

    // Transition backbuffer to render target
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = backbuffer;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

    cmdList->ResourceBarrier(1, &barrier);

    // Clear backbuffer
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_context->GetCurrentRTVHandle();

    float clearColor[4] = { 0.1f, 0.1f, 0.15f, 1.0f };
    cmdList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    // If sourceTexture exists, copy it to backbuffer
    if (sourceTexture)
    {
        // Copy texture using compute shader or copy command
        // This is simplified - real implementation would do proper copy
    }

    // NOTE: Leave backbuffer in RENDER_TARGET state for ImGui rendering
    // Will be transitioned to PRESENT in EndFrame
}