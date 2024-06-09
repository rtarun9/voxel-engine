#include "voxel-engine/timer.hpp"

Timer::Timer()
{
    // Get the performance counter frequency (in seconds).
    QueryPerformanceFrequency(&m_performance_frequency);

    m_seconds_per_count = 1.0f / (float)m_performance_frequency.QuadPart;
}

void Timer::start()
{
    QueryPerformanceCounter(&m_start_time);
}

void Timer::stop()
{
    QueryPerformanceCounter(&m_end_time);
}

float Timer::get_delta_time() const
{
    return (m_start_time.QuadPart - m_end_time.QuadPart) * m_seconds_per_count;
}