// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

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

		virtual void Visit(Renderable& r) = 0;
	};
}
