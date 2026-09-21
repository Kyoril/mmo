#pragma once

#include "base/typedefs.h"
#include "base/clock.h"
#include "game/time_of_day.h"

#include <algorithm>
#include <sstream>
#include <iomanip>
#include <string>

namespace mmo
{
    /**
     * @class GameTimeComponent
     * @brief A component that manages game time for day/night cycle functionality.
     * 
     * This component tracks game time and provides utilities to convert between
     * real time and game time based on a configurable speed multiplier.
     *
     * It can also blend from its current time of day to a new one over a short real-time
     * duration (TransitionTo). While such a transition runs, GetTime() and everything derived
     * from it report the blended time, while GetTargetTime() reports the authoritative clock
     * that is being blended towards.
     */
    class GameTimeComponent
    {
    public:
        /**
         * @brief Constructor to initialize the game time component.
         * @param initialTime Initial game time in milliseconds.
         * @param timeSpeed The speed at which game time passes relative to real time.
         */
        explicit GameTimeComponent(GameTime initialTime = 0, float timeSpeed = 1.0f)
            : m_gameTime(initialTime)
            , m_timeSpeed(timeSpeed)
            , m_lastUpdateTime(0)
            , m_transitionDelta(0)
            , m_transitionDurationMs(0)
            , m_transitionElapsedMs(0)
        {
        }

        /**
         * @brief Updates the game time based on the current real time.
         * @param currentRealTime Current real time in milliseconds.
         */
        void Update(GameTime currentRealTime)
        {
            if (m_lastUpdateTime == 0)
            {
                m_lastUpdateTime = currentRealTime;
                return;
            }

            // Calculate elapsed real time since last update
            const GameTime elapsedRealTime = currentRealTime - m_lastUpdateTime;
            
            // Convert to game time based on speed multiplier
            const GameTime elapsedGameTime = static_cast<GameTime>(elapsedRealTime * m_timeSpeed);
            
            // Update game time
            m_gameTime += elapsedGameTime;
            
            // Ensure game time wraps around every 24 hours (day/night cycle)
            m_gameTime %= constants::OneDay;

            // Advance a running transition in real time, independent of the time speed
            if (m_transitionDurationMs > 0)
            {
                m_transitionElapsedMs += elapsedRealTime;
                if (m_transitionElapsedMs >= m_transitionDurationMs)
                {
                    m_transitionDelta = 0;
                    m_transitionDurationMs = 0;
                    m_transitionElapsedMs = 0;
                }
            }
            
            // Update last update time
            m_lastUpdateTime = currentRealTime;
        }

        /**
         * @brief Forgets the real-time reference of the last Update.
         *
         * Call this when updates were paused for a while (e.g. the client left the world): the next
         * Update then only primes the reference instead of adding the whole pause to the clock.
         */
        void ResetUpdateReference() { m_lastUpdateTime = 0; }

        /**
         * @brief Gets the current game time in milliseconds.
         *
         * While a transition is running this is the blended time between the time of day the
         * transition started from and the target time.
         *
         * @return The current game time.
         */
        [[nodiscard]] GameTime GetTime() const
        {
            if (m_transitionDurationMs == 0)
            {
                return m_gameTime;
            }

            // Smoothstep easing so the sun accelerates and settles instead of jerking into motion
            const float t = std::min(1.0f, static_cast<float>(m_transitionElapsedMs) / static_cast<float>(m_transitionDurationMs));
            const float eased = t * t * (3.0f - 2.0f * t);
            const int64 remaining = static_cast<int64>(static_cast<double>(m_transitionDelta) * (1.0 - static_cast<double>(eased)));

            const int64 day = static_cast<int64>(constants::OneDay);
            const int64 blended = ((static_cast<int64>(m_gameTime) - remaining) % day + day) % day;
            return static_cast<GameTime>(blended);
        }

        /**
         * @brief Gets the authoritative game time in milliseconds, ignoring any running transition.
         * @return The game time a running transition blends towards, or the current game time.
         */
        [[nodiscard]] GameTime GetTargetTime() const { return m_gameTime; }

        /**
         * @brief Determines whether a time of day transition is currently running.
         */
        [[nodiscard]] bool IsTransitioning() const { return m_transitionDurationMs > 0; }

        /**
         * @brief Gets the current time speed multiplier.
         * @return The current time speed multiplier.
         */
        [[nodiscard]] float GetTimeSpeed() const { return m_timeSpeed; }

        /**
         * @brief Sets the current game time immediately, cancelling any running transition.
         * @param gameTime The new game time in milliseconds.
         */
        void SetTime(GameTime gameTime) 
        { 
            m_gameTime = gameTime % constants::OneDay; 
            m_transitionDelta = 0;
            m_transitionDurationMs = 0;
            m_transitionElapsedMs = 0;
        }

        /**
         * @brief Re-synchronizes the authoritative game time without disturbing a running transition.
         *
         * Use this for periodic clock corrections: a running transition keeps blending, it just
         * lands on the corrected time.
         *
         * @param gameTime The authoritative game time in milliseconds.
         */
        void SyncTime(GameTime gameTime)
        {
            m_gameTime = gameTime % constants::OneDay;
        }

        /**
         * @brief Blends smoothly from the currently reported time to a new game time.
         *
         * The authoritative time jumps to the new value right away (GetTargetTime), while GetTime
         * travels the shorter way around the clock to it over the given real-time duration. A
         * transition that is already running is continued from wherever it currently is.
         *
         * @param gameTime The new game time in milliseconds.
         * @param durationMs Real-time duration of the blend in milliseconds. 0 sets the time immediately.
         */
        void TransitionTo(GameTime gameTime, GameTime durationMs)
        {
            const GameTime from = GetTime();
            const GameTime to = gameTime % constants::OneDay;
            const int64 delta = GetShortestTimeOfDayDelta(from, to);

            SetTime(to);
            if (durationMs == 0 || delta == 0)
            {
                return;
            }

            m_transitionDelta = delta;
            m_transitionDurationMs = durationMs;
            m_transitionElapsedMs = 0;
        }

        /**
         * @brief Sets the game time speed multiplier.
         * @param timeSpeed The new time speed multiplier.
         */
        void SetTimeSpeed(float timeSpeed) { m_timeSpeed = timeSpeed; }

        /**
         * @brief Gets the hour component of the current game time (0-23).
         * @return The current hour.
         */
        [[nodiscard]] uint32 GetHour() const
        {
            return (GetTime() / constants::OneHour) % 24;
        }

        /**
         * @brief Gets the minute component of the current game time (0-59).
         * @return The current minute.
         */
        [[nodiscard]] uint32 GetMinute() const
        {
            return (GetTime() / constants::OneMinute) % 60;
        }

        /**
         * @brief Gets the second component of the current game time (0-59).
         * @return The current second.
         */
        [[nodiscard]] uint32 GetSecond() const
        {
            return (GetTime() / constants::OneSecond) % 60;
        }

        /**
         * @brief Gets a formatted time string (HH:MM:SS).
         * @return Formatted time string.
         */
        [[nodiscard]] std::string GetTimeString() const
        {
            std::stringstream ss;
            ss << std::setfill('0') << std::setw(2) << GetHour() << ":"
               << std::setfill('0') << std::setw(2) << GetMinute() << ":"
               << std::setfill('0') << std::setw(2) << GetSecond();
            return ss.str();
        }

        /**
         * @brief Gets a normalized time of day value (0.0 to 1.0).
         * 
         * 0.0 = Midnight (00:00)
         * 0.25 = Dawn (06:00)
         * 0.5 = Noon (12:00)
         * 0.75 = Dusk (18:00)
         * 
         * @return Normalized time of day.
         */
        [[nodiscard]] float GetNormalizedTimeOfDay() const
        {
            return static_cast<float>(GetTime()) / static_cast<float>(constants::OneDay);
        }

    private:
        GameTime m_gameTime;      ///< Current game time in milliseconds
        float m_timeSpeed;        ///< Game time speed multiplier
        GameTime m_lastUpdateTime; ///< Last real-time when game time was updated
        int64 m_transitionDelta;          ///< Signed distance the running transition still covers at its start
        GameTime m_transitionDurationMs;  ///< Real-time length of the running transition, 0 if none
        GameTime m_transitionElapsedMs;   ///< Real time elapsed since the running transition started
    };
}
