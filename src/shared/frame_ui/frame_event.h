// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string>


namespace mmo
{
	/// This class represents frame event data.
	class FrameEvent final
	{
	public:
		/// Execution operator for executing the assigned lua code.
		void operator()() const;
		/// Assigns the given lua code to this event.
		void Set(std::string code);

	private:
		/// Lua code to be executed when this event is triggered.
		std::string m_luaCode;
	};
}
