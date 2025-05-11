#pragma once

#include "base/typedefs.h"
#include "base/clock.h"
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
            
            // Update last update time
            m_lastUpdateTime = currentRealTime;
        }

        /**
         * @brief Gets the current game time in milliseconds.
         * @return The current game time.
         */
        [[nodiscard]] GameTime GetTime() const { return m_gameTime; }

        /**
         * @brief Gets the current time speed multiplier.
         * @return The current time speed multiplier.
         */
        [[nodiscard]] float GetTimeSpeed() const { return m_timeSpeed; }

        /**
         * @brief Sets the current game time.
         * @param gameTime The new game time in milliseconds.
         */
        void SetTime(GameTime gameTime) 
        { 
            m_gameTime = gameTime % constants::OneDay; 
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
            return (m_gameTime / constants::OneHour) % 24;
        }

        /**
         * @brief Gets the minute component of the current game time (0-59).
         * @return The current minute.
         */
        [[nodiscard]] uint32 GetMinute() const
        {
            return (m_gameTime / constants::OneMinute) % 60;
        }

        /**
         * @brief Gets the second component of the current game time (0-59).
         * @return The current second.
         */
        [[nodiscard]] uint32 GetSecond() const
        {
            return (m_gameTime / constants::OneSecond) % 60;
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
            return static_cast<float>(m_gameTime) / static_cast<float>(constants::OneDay);
        }

    private:
        GameTime m_gameTime;      ///< Current game time in milliseconds
        float m_timeSpeed;        ///< Game time speed multiplier
        GameTime m_lastUpdateTime; ///< Last real-time when game time was updated
    };
}
