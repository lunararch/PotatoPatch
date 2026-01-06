#pragma once
#include <chrono>

class Timer
{
public:
    void Start();
    void Tick();
    float GetDeltaTime() const { return m_deltaTime; }
    float GetTotalTime() const;

private:
    std::chrono::high_resolution_clock::time_point m_startTime;
    std::chrono::high_resolution_clock::time_point m_lastTime;
    float m_deltaTime = 0.0f;
};