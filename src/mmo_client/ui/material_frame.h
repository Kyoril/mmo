#pragma once

#include "frame_ui/frame.h"

namespace mmo
{
	class MaterialFrame final : public Frame
	{
	public:
		MaterialFrame(const std::string& type, const std::string& name);
		~MaterialFrame() override;

	public:
		void Copy(Frame& other) override;
		void Update(float elapsed) override;

	protected:
		void DrawSelf() override;
	};
}