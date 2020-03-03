
#include "frame_layer.h"
#include "geometry_buffer.h"

#include "base/macros.h"

#include <utility>


namespace mmo
{
	inline FrameLayer::FrameLayer(std::string name)
		: m_name(std::move(name))
	{
	}

	void FrameLayer::AddObject(std::unique_ptr<FrameObject> object)
	{
		ASSERT(object);

		m_objects.emplace_back(std::move(object));
	}

	void FrameLayer::RemoveAllObjects()
	{
		m_objects.clear();
	}

	void FrameLayer::Render(GeometryBuffer& buffer) const
	{
		for (const auto& object : m_objects)
		{
			object->Render(buffer);
		}
	}
}
