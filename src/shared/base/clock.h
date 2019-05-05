// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "typedefs.h"

namespace mmo
{
	namespace constants
	{
		static const GameTime OneSecond = 1000;
		static const GameTime OneMinute = OneSecond * 60;
		static const GameTime OneHour = OneMinute * 60;
		static const GameTime OneDay = OneHour * 24;
	}

	template <class T>
	T gameTimeToSeconds(GameTime time)
	{
		return static_cast<T>(time) / static_cast<T>(constants::OneSecond);
	}


	template <class T>
	GameTime gameTimeFromSeconds(T seconds)
	{
		return static_cast<GameTime>(seconds * static_cast<T>(constants::OneSecond));
	}


	GameTime getCurrentTime();
}
