#include "pch.hpp"

#include "voxel-engine/timer.hpp"

timer_t::timer_t()
{
    // Get the performance counter frequency (in seconds).
    QueryPerformanceFrequency(&m_performance_frequency);

    m_seconds_per_count = 1.0f / (f32)m_performance_frequency.QuadPart;
}

f32 timer_t::tick_and_get_delta_time_seconds()
{
    QueryPerformanceCounter(&m_end_time);
    f32 delta_time = (m_end_time.QuadPart - m_start_time.QuadPart) * m_seconds_per_count;

    m_start_time.QuadPart = m_end_time.QuadPart;

    return delta_time;
}