// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "foliage_layer.h"

#include <cmath>

namespace mmo
{
	FoliageLayer::FoliageLayer(const String& name, MeshPtr mesh)
		: m_name(name)
		, m_mesh(std::move(mesh))
		, m_material(nullptr)
	{
		// If no custom material is provided, try to get one from the mesh
		if (m_mesh && m_mesh->GetSubMeshCount() > 0)
		{
			m_material = m_mesh->GetSubMesh(0).GetMaterial();
		}
	}

	FoliageLayer::FoliageLayer(const String& name, MeshPtr mesh, MaterialPtr material)
		: m_name(name)
		, m_mesh(std::move(mesh))
		, m_material(std::move(material))
	{
	}

	void FoliageLayer::SetMesh(MeshPtr mesh)
	{
		m_mesh = std::move(mesh);
		m_dirty = true;
	}

	void FoliageLayer::SetMaterial(MaterialPtr material)
	{
		m_material = std::move(material);
		m_dirty = true;
	}

	void FoliageLayer::SetSettings(const FoliageLayerSettings& settings)
	{
		m_settings = settings;
		m_dirty = true;
	}

	void FoliageLayer::SetDensity(const float density)
	{
		m_settings.density = density;
		m_dirty = true;
	}

	void FoliageLayer::SetScaleRange(const float minScale, const float maxScale)
	{
		m_settings.minScale = minScale;
		m_settings.maxScale = maxScale;
		m_dirty = true;
	}

	void FoliageLayer::SetHeightRange(const float minHeight, const float maxHeight)
	{
		m_settings.minHeight = minHeight;
		m_settings.maxHeight = maxHeight;
		m_dirty = true;
	}

	void FoliageLayer::SetFadeDistances(const float startDistance, const float endDistance)
	{
		m_settings.fadeStartDistance = startDistance;
		m_settings.fadeEndDistance = endDistance;
		m_dirty = true;
	}

	bool FoliageLayer::IsValidPlacement(const Vector3& position, const float slopeAngle) const
	{
		// Check height range
		if (position.y < m_settings.minHeight || position.y > m_settings.maxHeight)
		{
			return false;
		}

		// Check slope angle
		if (slopeAngle > m_settings.maxSlopeAngle)
		{
			return false;
		}

		return true;
	}

	float FoliageLayer::GenerateRandomScale(std::mt19937& rng) const
	{
		std::uniform_real_distribution<float> dist(m_settings.minScale, m_settings.maxScale);
		return dist(rng);
	}

	float FoliageLayer::GenerateRandomYaw(std::mt19937& rng) const
	{
		if (!m_settings.randomYawRotation)
		{
			return 0.0f;
		}

		constexpr float twoPi = 6.28318530718f;
		std::uniform_real_distribution<float> dist(0.0f, twoPi);
		return dist(rng);
	}

	float FoliageLayer::CalculateFadeFactor(const float distance) const
	{
		if (distance <= m_settings.fadeStartDistance)
		{
			return 1.0f;
		}

		if (distance >= m_settings.fadeEndDistance)
		{
			return 0.0f;
		}

		// Linear interpolation between start and end distances
		const float range = m_settings.fadeEndDistance - m_settings.fadeStartDistance;
		const float t = (distance - m_settings.fadeStartDistance) / range;
		return 1.0f - t;
	}
}
