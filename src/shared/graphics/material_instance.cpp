// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "material_instance.h"
#include "graphics/graphics_device.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"

namespace mmo
{
	MaterialInstance::MaterialInstance(const std::string_view name, MaterialPtr parent)
		: m_name(name)
		, m_parent(nullptr)
	{
		ASSERT(m_parent);

		SetParent(parent);
	}

	void MaterialInstance::SetParent(MaterialPtr parent) noexcept
	{
		ASSERT(m_parent);

		// Parent must be valid
		if (!parent)
		{
			return;
		}

		// Anything new at all?
		if (m_parent == parent)
		{
			return;
		}

		// Can't set ourself as parent
		if (parent.get() == this)
		{
			return;
		}

		// TODO: We also need to prevent a parent loop. A material instance parent could be a material instance as well, whose parent could again be us.

		const bool hadParent = m_parent != nullptr;

		// Update parent
		m_parent = parent;

		// Refresh base values from parent material if this is the first reference to a parent material
		if (!hadParent)
		{
			DerivePropertiesFromParent();
		}
	}

	void MaterialInstance::DerivePropertiesFromParent()
	{
		m_type = m_parent->GetType();
		m_receiveShadows = m_parent->IsReceivingShadows();
		m_castShadows = m_parent->IsCastingShadows();
		m_twoSided = m_parent->IsTwoSided();
	}

	void MaterialInstance::Update()
	{
		m_parent->Update();
	}

	void MaterialInstance::Apply(GraphicsDevice& device)
	{
		m_parent->Apply(device);
	}
}
