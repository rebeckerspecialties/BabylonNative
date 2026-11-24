#pragma once

#include <algorithm>
#include <chrono>

namespace Babylon
{
    // MediaCodec texture playback has no audio sink to drive time for a silent track.
    class VideoPlaybackClock
    {
        using Clock = std::chrono::steady_clock;
        Clock::time_point m_start{};
        int m_offset{}, m_duration{};
        bool m_playing{}, m_loop{};

    public:
        void Reset(int duration)
        {
            *this = {};
            m_duration = std::max(duration, 0);
        }
        void SetPosition(int position, Clock::time_point now)
        {
            m_offset = position;
            m_start = now;
        }
        int Position(Clock::time_point now) const
        {
            auto ms = m_offset + (m_playing ? std::chrono::duration_cast<std::chrono::milliseconds>(now - m_start).count() : 0);
            if (m_duration == 0) return 0;
            return static_cast<int>(m_loop ? ms % m_duration : std::min<decltype(ms)>(ms, m_duration));
        }
        void Play(bool loop, Clock::time_point now)
        {
            if (m_duration > 0 && Position(now) == m_duration) SetPosition(0, now);
            else if (!m_playing) m_start = now;
            m_loop = loop;
            m_playing = true;
        }
        void Pause(Clock::time_point now)
        {
            SetPosition(Position(now), now);
            m_playing = false;
        }
    };
}
