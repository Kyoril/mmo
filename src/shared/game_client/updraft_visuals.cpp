// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "updraft_visuals.h"

#include "particle_params_cache.h"
#include "updraft_placement.h"

#include "scene_graph/particle_emitter.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"

#include <algorithm>
#include <string>

namespace mmo
{
	namespace
	{
		/// Footprint area (square metres) the authored spawn rates are tuned for.
		constexpr float ReferenceFootprint = 36.0f;
	}

	UpdraftVisuals::UpdraftVisuals(Scene& scene)
		: m_scene(scene)
	{
	}

	UpdraftVisuals::~UpdraftVisuals()
	{
		Clear();
	}

	void UpdraftVisuals::Show(const std::vector<UpdraftVolume>& volumes)
	{
		Clear();

		const ParticleSystemParameters* source = LoadCachedParticleSystem(ColumnParticles);
		if (!source)
		{
			return;
		}

		static uint64 s_counter = 0;
		for (const UpdraftVolume& volume : volumes)
		{
			if (volume.strength <= 0.0f)
			{
				continue;
			}

			const UpdraftEmitterPlacement placement = GetUpdraftEmitterPlacement(volume);
			const float footprint = 4.0f * placement.halfWidth * placement.halfDepth;
			const float density = std::clamp(footprint / ReferenceFootprint, 0.25f, 4.0f);

			// Box emitters cover the footprint; spawn rates follow its area so a wide updraft
			// is as dense as a narrow one.
			ParticleSystemParameters params = *source;
			for (EmitterParameters& emitter : params.emitters)
			{
				if (emitter.shape == EmitterShape::Box)
				{
					emitter.shapeExtents = Vector3(placement.halfWidth * 2.0f, emitter.shapeExtents.y, placement.halfDepth * 2.0f);
				}
				emitter.spawnRate *= density;
				emitter.maxParticles = static_cast<uint32>(static_cast<float>(emitter.maxParticles) * density);
			}

			const std::string name = "Updraft_" + std::to_string(s_counter++);
			Column column;
			column.emitter = m_scene.CreateParticleEmitter(name);
			if (!column.emitter)
			{
				continue;
			}

			column.emitter->SetSystemParameters(params);
			column.node = m_scene.GetRootSceneNode().CreateChildSceneNode(name + "_Node");
			column.node->SetPosition(placement.base);
			column.node->SetOrientation(Quaternion(Radian(placement.yaw), Vector3::UnitY));
			column.node->AttachObject(*column.emitter);
			column.emitter->Play();
			m_columns.push_back(column);
		}
	}

	void UpdraftVisuals::Clear()
	{
		for (const Column& column : m_columns)
		{
			column.emitter->Stop();
			m_scene.DestroyParticleEmitter(*column.emitter);
			if (column.node)
			{
				m_scene.DestroySceneNode(*column.node);
			}
		}

		m_columns.clear();
	}
}
