// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "debug_interface.h"

namespace mmo
{
	static DebugInterface* s_debugInterface = nullptr;

	DebugInterface* GetDebugInterface()
	{
		return s_debugInterface;
	}

	void SetDebugInterface(DebugInterface* debugInterface)
	{
		s_debugInterface = debugInterface;
	}
}