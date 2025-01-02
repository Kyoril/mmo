#pragma once

#include "frame.h"

namespace mmo
{
	class ScrollFrame final : public Frame
	{
	public:
		explicit ScrollFrame(const std::string& name, const std::string& type);
		~ScrollFrame() override = default;

	public:

	};
}
