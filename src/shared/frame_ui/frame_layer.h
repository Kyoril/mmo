
#pragma once

#include "frame_object.h"

#include "base/non_copyable.h"

#include <memory>
#include <vector>


namespace mmo
{
	class GeometryBuffer;


	/// This class represents a layer of a frame. Layers contain assigned objects
	/// that can be rendered in order.
	class FrameLayer : public NonCopyable
	{
	public:
		FrameLayer() {}
		
	public:
		/// Adds a new object to the object list of this layer.
		void AddObject(std::unique_ptr<FrameObject> object);
		/// Removes all objects from this layer.
		void RemoveAllObjects();
		/// Renders the frame layer, which simply means rendering all attached objects
		/// in order.
		void Render(GeometryBuffer& buffer) const;

	protected:
		/// 
		std::vector<std::unique_ptr<FrameObject>> m_objects;
	};
}
