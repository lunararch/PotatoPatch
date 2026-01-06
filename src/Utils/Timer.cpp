#include "Timer.h"

void Timer::Start()
{
    m_startTime = std::chrono::high_resolution_clock::now();
    m_lastTime = m_startTime;
}

void Timer::Tick()
{
    auto currentTime = std::chrono::high_resolution_clock::now();
    m_deltaTime = std::chrono::duration<float>(currentTime - m_lastTime).count();
    m_lastTime = currentTime;
}

float Timer::GetTotalTime() const
{
    auto currentTime = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<float>(currentTime - m_startTime).count();
}