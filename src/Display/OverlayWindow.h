#pragma once
#include <Windows.h>
#include <string>
#include <memory>
#include "OverlayRenderer.h"
#include "../Capture/DesktopDuplication.h"

class OverlayWindow
{
public:
    OverlayWindow();
    ~OverlayWindow();

    // Initialize the overlay system
    bool Initialize(HINSTANCE hInstance);
    void Shutdown();
    
    // Set the target window to overlay
    bool SetTargetWindow(HWND targetWindow);
    
    // Start/stop the overlay
    bool StartOverlay(DesktopDuplication* capture);
    void StopOverlay();
    
    // Update overlay position to match target window
    void UpdatePosition();
    
    // Process one frame (capture, process, display)
    void ProcessFrame();
    
    // Check if overlay is active
    bool IsActive() const { return m_overlayActive; }
    
    // Get the overlay window handle
    HWND GetOverlayHwnd() const { return m_overlayHwnd; }
    
    // Get stats
    uint32_t GetFramesCaptured() const { return m_framesCaptured; }
    float GetOverlayFPS() const { return m_overlayFPS; }
    
    // Upscaling controls - forwarded to OverlayRenderer
    void SetUpscalingEnabled(bool enabled);
    bool IsUpscalingEnabled() const;
    
    void SetUpscaleMethod(UpscaleMethod method);
    UpscaleMethod GetUpscaleMethod() const;
    
    void SetUpscaleFactor(float factor);
    float GetUpscaleFactor() const;
    
    void SetSharpness(float sharpness);
    float GetSharpness() const;

private:
    static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    bool CreateOverlayWindow(HINSTANCE hInstance);
    void PositionOverlayOverTarget();

private:
    HWND m_overlayHwnd = nullptr;
    HWND m_targetWindow = nullptr;
    
    std::unique_ptr<OverlayRenderer> m_renderer;
    DesktopDuplication* m_capture = nullptr;
    
    bool m_overlayActive = false;
    uint32_t m_framesCaptured = 0;
    
    // Upscaling settings (cached for when renderer isn't active)
    bool m_upscaleEnabled = false;
    UpscaleMethod m_upscaleMethod = UpscaleMethod::FSR;
    float m_upscaleFactor = 1.5f;
    float m_sharpness = 0.5f;
    
    // FPS tracking
    LARGE_INTEGER m_lastFrameTime = {};
    LARGE_INTEGER m_frequency = {};
    float m_overlayFPS = 0.0f;
    float m_fpsAccumulator = 0.0f;
    int m_fpsFrameCount = 0;
    
    // Target window info
    RECT m_targetRect = {};
    int m_targetMonitor = -1;
};
