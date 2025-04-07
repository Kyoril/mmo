// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file clock.h
 *
 * @brief Provides time-related utilities and constants.
 *
 * This file contains functions and constants for handling game time,
 * including time conversion utilities and platform-independent time
 * measurement functions.
 */

#pragma once

#include "typedefs.h"

namespace mmo
{
	/**
	 * @namespace mmo::constants
	 * @brief Contains time-related constants.
	 */
	namespace constants
	{
		/**
		 * @brief Represents one second in game time (1000 milliseconds).
		 */
		static const GameTime OneSecond = 1000;
		
		/**
		 * @brief Represents one minute in game time (60,000 milliseconds).
		 */
		static const GameTime OneMinute = OneSecond * 60;
		
		/**
		 * @brief Represents one hour in game time (3,600,000 milliseconds).
		 */
		static const GameTime OneHour = OneMinute * 60;
		
		/**
		 * @brief Represents one day in game time (86,400,000 milliseconds).
		 */
		static const GameTime OneDay = OneHour * 24;
	}

	/**
	 * @brief Converts game time (in milliseconds) to seconds.
	 * 
	 * @tparam T The numeric type to return (typically float or double).
	 * @param time The game time value in milliseconds.
	 * @return The time value converted to seconds as type T.
	 */
	template <class T>
	T gameTimeToSeconds(GameTime time)
	{
		return static_cast<T>(time) / static_cast<T>(constants::OneSecond);
	}

	/**
	 * @brief Converts seconds to game time (in milliseconds).
	 * 
	 * @tparam T The numeric type of the input seconds (typically float or double).
	 * @param seconds The time value in seconds.
	 * @return The time value converted to game time (milliseconds).
	 */
	template <class T>
	GameTime gameTimeFromSeconds(T seconds)
	{
		return static_cast<GameTime>(seconds * static_cast<T>(constants::OneSecond));
	}

	/**
	 * @brief Gets the current system time in milliseconds.
	 * 
	 * This function provides a platform-independent way to get the current
	 * system time with millisecond precision. It uses the most appropriate
	 * high-resolution timer available on each platform.
	 * 
	 * @return The current system time in milliseconds.
	 */
	GameTime GetAsyncTimeMs();
}
