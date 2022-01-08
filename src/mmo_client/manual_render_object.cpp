// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "manual_render_object.h"

namespace mmo
{
	ManualRenderObject::ManualRenderObject(GraphicsDevice& device)
		: m_device(device)
	{
	}

	ManualRenderOperationRef<ManualLineListOperation> ManualRenderObject::AddLineListOperation()
	{
		auto operation = std::make_unique<ManualLineListOperation>(m_device);
		const auto result = operation.get();

		m_operations.emplace_back(std::move(operation));

		return *result;
	}

	void ManualRenderObject::Clear() noexcept
	{
		m_operations.clear();
	}

	void ManualRenderObject::Render() const
	{
		for (const auto& operation : m_operations)
		{
			operation->Render();
		}
	}
}
