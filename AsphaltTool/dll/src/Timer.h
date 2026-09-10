#pragma once

//std
#include <chrono>

#include "Units.h"

namespace AsphaltDLL
{
    class Timer
    {   
        private:
            enum class TimerState : int
            {
                UNINITIALIZED, PAUSED, RUNNING
            };

            Units::MicroSecond m_begin_time           {0};
            Units::MicroSecond m_time_last_pause      {0};
            Units::MicroSecond m_pause_time_counter   {0};

            TimerState m_state = TimerState::UNINITIALIZED;

        public:

            Timer() noexcept
            { 
                Restart(); 
            }

            void Restart() noexcept
            {
                m_state = TimerState::RUNNING;
                m_begin_time = GetMonotonicTime<Units::MicroSecond>();
                m_pause_time_counter = Units::MicroSecond(0);
            }

            void Continue() noexcept
            {
                if(m_state == TimerState::UNINITIALIZED) 
                {
                    Restart();
                    return;
                }

                if(m_state == TimerState::RUNNING) 
                {   
                    return;
                }

                m_state = TimerState::RUNNING;
                m_pause_time_counter += GetMonotonicTime<Units::MicroSecond>() - m_time_last_pause;
            }

            void Pause() noexcept
            {
                if(m_state != TimerState::RUNNING) 
                {
                    return;
                }
                m_state = TimerState::PAUSED;
                m_time_last_pause = GetMonotonicTime<Units::MicroSecond>();
            }

            void Cancel() noexcept
            {
                m_state = TimerState::UNINITIALIZED;
                m_begin_time = m_time_last_pause = m_pause_time_counter = Units::MicroSecond(0);
            }

            template <typename T> 
            requires Units::Is_Time_Unit<T>
            [[nodiscard]] inline T GetElapsedAndRestart() noexcept 
            {
                T elapsed (GetElapsed<T>());
                Restart();
                return elapsed;
            }

            template <typename T> 
            requires Units::Is_Time_Unit<T>
            [[nodiscard]] inline T GetElapsed() const noexcept 
            {
                const Units::MicroSecond time_now = Timer::GetMonotonicTime<Units::MicroSecond>();

                if (m_state == TimerState::PAUSED) 
                    return Units::Convert<T>((time_now - m_begin_time) - m_pause_time_counter - (time_now - m_time_last_pause));

                else if (m_state == TimerState::RUNNING) 
                    return Units::Convert<T>((time_now - m_begin_time) - m_pause_time_counter);
                else
                    assert(false && "At Timer::GetElapsed(): Trying to read state while uninitialized.");
                return T {};
            }

            template <typename T>
            requires Units::Is_Time_Unit<T>
            [[nodiscard]] inline bool AtLeastElapsed(T minimum) const noexcept
            {
                return GetElapsed<T>() >= minimum;
            }

            template <typename T> 
            requires Units::Is_Time_Unit<T>
            [[nodiscard]] static inline T GetMonotonicTime() noexcept 
            {
                const Units::MicroSecond time_now (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
                return Units::Convert<T>(time_now);
            }
    };
}