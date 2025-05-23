// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	class Pass;
	class Renderable;

	struct RenderablePass
	{
		Renderable* renderable;
		Pass* pass;

		RenderablePass(Renderable* rend, Pass* p)
			: renderable(rend)
			, pass(p)
		{
		}
	};

	class QueuedRenderableVisitor
	{
	public:
		virtual ~QueuedRenderableVisitor() = default;

	public:
		virtual void Visit(RenderablePass& rp) = 0;
		
		virtual bool Visit(const Pass& p) = 0;

		virtual void Visit(Renderable& r, uint32 groupId) = 0;
	};
}
