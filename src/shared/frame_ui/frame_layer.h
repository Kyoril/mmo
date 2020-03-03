
#pragma once

#include "frame_object.h"

#include "base/non_copyable.h"

#include <string>
#include <memory>
#include <vector>


namespace mmo
{
	class GeometryBuffer;


	/// This class represents a layer of a frame. Layers contain assigned objects
	/// that can be rendered in order.
	class FrameLayer final
		: public NonCopyable
	{
	public:
		FrameLayer(std::string name);
		
	public:
		/// Gets the name of this layer.
		inline const std::string& GetName() const { return m_name; }

	public:
		/// Adds a new object to the object list of this layer.
		void AddObject(std::unique_ptr<FrameObject> object);
		/// Removes all objects from this layer.
		void RemoveAllObjects();
		/// Renders the frame layer, which simply means rendering all attached objects
		/// in order.
		void Render(GeometryBuffer& buffer) const;

	protected:
		/// Name of the layer (if any).
		std::string m_name;
		/// 
		std::vector<std::unique_ptr<FrameObject>> m_objects;
	};
}
