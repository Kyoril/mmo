
#pragma once

#include "base/non_copyable.h"

namespace mmo
{
	class GeometryBuffer;

	/// This is the base class for a frame object which is renderable
	/// and has some placement logic.
	class FrameObject : public NonCopyable
	{
	public:

		FrameObject() {}
		virtual ~FrameObject() = default;

	public:

		/// Renders the frame object.
		virtual void Render(GeometryBuffer& buffer) const = 0;
	};
}
