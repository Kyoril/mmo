// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "frame_ui/frame.h"

namespace mmo
{
	/// Frame for rendering the actual 3d game world.
	class WorldFrame final
		: public Frame
	{
	public:
		/// Default constructor.
		explicit WorldFrame(const std::string& name);

	public:

		void RenderWorld();

		void SetAsCurrentWorldFrame();
	};
}