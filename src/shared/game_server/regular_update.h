// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// Contains data of a regular update.
	class RegularUpdate
	{
	public:
		RegularUpdate(GameTime timestamp, float deltaSeconds)
			: m_timestamp(timestamp)
			, m_deltaSeconds(deltaSeconds)
		{
		}

	public:
		/// Gets the timestamp of the start of this regular update.
		GameTime GetTimestamp() const { return m_timestamp; }

		/// Gets the amount of seconds since the last regular update for the updated instance.
		float GetDeltaSeconds() const { return m_deltaSeconds; }
	
	private:
		GameTime m_timestamp;
		float m_deltaSeconds;
	};
}