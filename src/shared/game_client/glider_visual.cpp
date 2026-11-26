// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glider_visual.h"

#include "particle_params_cache.h"

#include "assets/asset_registry.h"
#include "scene_graph/entity.h"
#include "scene_graph/particle_emitter.h"
#include "scene_graph/scene.h"
#include "scene_graph/skeleton_instance.h"
#include "scene_graph/tag_point.h"

#include <algorithm>

namespace mmo
{
	namespace
	{
		/// Wing tips in glider mesh space.
		const std::array<Vector3, 2> s_wingTips = { Vector3(-1.5f, 0.21f, -0.26f), Vector3(1.5f, 0.21f, -0.26f) };

		/// Smallest span scale of a visible glider, so a just-opened glider is never degenerate.
		constexpr float MinSpanScale = 0.05f;

		/// Ease-out unfold: fast at the start, settling into the open span.
		float SpanScale(const float unfold)
		{
			const float inverse = 1.0f - unfold;
			return std::max(1.0f - inverse * inverse, MinSpanScale);
		}
	}

	GliderVisual::GliderVisual(Scene& scene, Entity& host, const uint64 guid, const GliderAttachment& attachment)
		: m_scene(scene)
		, m_host(host)
		, m_attachment(attachment)
		, m_guid(guid)
	{
		if (!m_host.HasSkeleton() || !m_host.GetSkeleton()->HasBone(m_attachment.bone) || !AssetRegistry::HasFile(m_attachment.mesh))
		{
			return;
		}

		m_glider = m_scene.CreateEntity(m_host.GetName() + "_Glider", m_attachment.mesh);
		if (!m_glider)
		{
			return;
		}

		// The glider never blocks movement or picking.
		m_glider->SetCollisionEnabled(false);
		m_glider->SetVisible(false);
		m_gliderAttachment = m_host.AttachObjectToBone(m_attachment.bone, *m_glider, m_attachment.rotation, m_attachment.offset);

		for (size_t i = 0; i < m_trails.size(); ++i)
		{
			m_trails[i] = CreateEmitter(TrailParticles, i == 0 ? "_GliderTrailL" : "_GliderTrailR");
			if (m_trails[i])
			{
				m_trails[i]->Stop();
				m_host.AttachObjectToBone(m_attachment.bone, *m_trails[i], Quaternion::Identity, m_attachment.offset + m_attachment.rotation * (s_wingTips[i] * m_attachment.scale));
			}
		}
	}

	GliderVisual::~GliderVisual()
	{
		for (auto& trail : m_trails)
		{
			DestroyEmitter(trail);
		}
		DestroyEmitter(m_openBurst);

		if (m_glider)
		{
			m_host.DetachObjectFromBone(*m_glider);
			m_scene.DestroyEntity(*m_glider);
			m_glider = nullptr;
			m_gliderAttachment = nullptr;
		}
	}

	glider_event::Type GliderVisual::Update(const bool gliding, const float deltaTime)
	{
		const glider_event::Type event = m_fold.Update(gliding, deltaTime);
		if (!m_glider)
		{
			return event;
		}

		if (event == glider_event::Opened)
		{
			m_glider->SetVisible(true);

			DestroyEmitter(m_openBurst);
			m_openBurst = CreateEmitter(OpenParticles, "_GliderOpen");
			if (m_openBurst)
			{
				m_host.AttachObjectToBone(m_attachment.bone, *m_openBurst, Quaternion::Identity, m_attachment.offset);
				m_openBurst->Play();
			}
		}
		else if (event == glider_event::Closed)
		{
			m_glider->SetVisible(false);
		}

		if (m_gliderAttachment)
		{
			m_gliderAttachment->SetScale(Vector3(SpanScale(m_fold.GetUnfold()) * m_attachment.scale.x, m_attachment.scale.y, m_attachment.scale.z));
		}

		// The trails stream only from a fully open glider.
		const bool trailsWanted = gliding && m_fold.GetUnfold() >= 1.0f;
		if (trailsWanted != m_trailsPlaying)
		{
			m_trailsPlaying = trailsWanted;
			for (ParticleSystem* trail : m_trails)
			{
				if (!trail)
				{
					continue;
				}

				if (trailsWanted)
				{
					trail->Play();
				}
				else
				{
					trail->Stop();
				}
			}
		}

		return event;
	}

	ParticleSystem* GliderVisual::CreateEmitter(const char* file, const char* suffix)
	{
		const ParticleSystemParameters* params = LoadCachedParticleSystem(file);
		if (!params)
		{
			return nullptr;
		}

		// Unique per emitter: units despawning and respawning overlap with the same guid.
		static uint64 s_counter = 0;
		ParticleSystem* emitter = m_scene.CreateParticleEmitter(
			"Glider_" + std::to_string(m_guid) + suffix + "_" + std::to_string(s_counter++));
		if (emitter)
		{
			emitter->SetSystemParameters(*params);
		}

		return emitter;
	}

	void GliderVisual::DestroyEmitter(ParticleSystem*& emitter)
	{
		if (!emitter)
		{
			return;
		}

		m_host.DetachObjectFromBone(*emitter);
		m_scene.DestroyParticleEmitter(*emitter);
		emitter = nullptr;
	}
}
