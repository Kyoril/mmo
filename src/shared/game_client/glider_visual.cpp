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
		/// Placement of the glider mesh relative to the attach bone. The mesh has its origin at
		/// the harness, the span along +-X, forward +Z and up +Y, like the character model; the
		/// player rigs' Back_M bone is turned -90 degrees about the model's up axis, which this
		/// orientation undoes. The offset moves the harness from Back_M (a holster bone a hand's
		/// width behind the back) onto the back. The Glide pose's grips match these values.
		const Quaternion s_gliderOrientation = Quaternion(0.70710678f, 0.0f, 0.70710678f, 0.0f);
		const Vector3 s_gliderOffset = Vector3(0.10f, 0.05f, 0.0f);

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

	GliderVisual::GliderVisual(Scene& scene, Entity& host, const uint64 guid)
		: m_scene(scene)
		, m_host(host)
		, m_guid(guid)
	{
		if (!m_host.HasSkeleton() || !m_host.GetSkeleton()->HasBone(AttachBone) || !AssetRegistry::HasFile(MeshFile))
		{
			return;
		}

		m_glider = m_scene.CreateEntity(m_host.GetName() + "_Glider", MeshFile);
		if (!m_glider)
		{
			return;
		}

		// The glider never blocks movement or picking.
		m_glider->SetCollisionEnabled(false);
		m_glider->SetVisible(false);
		m_gliderAttachment = m_host.AttachObjectToBone(AttachBone, *m_glider, s_gliderOrientation, s_gliderOffset);

		for (size_t i = 0; i < m_trails.size(); ++i)
		{
			m_trails[i] = CreateEmitter(TrailParticles, i == 0 ? "_GliderTrailL" : "_GliderTrailR");
			if (m_trails[i])
			{
				m_trails[i]->Stop();
				m_host.AttachObjectToBone(AttachBone, *m_trails[i], Quaternion::Identity, s_gliderOffset + s_gliderOrientation * s_wingTips[i]);
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
				m_host.AttachObjectToBone(AttachBone, *m_openBurst, Quaternion::Identity, s_gliderOffset);
				m_openBurst->Play();
			}
		}
		else if (event == glider_event::Closed)
		{
			m_glider->SetVisible(false);
		}

		if (m_gliderAttachment)
		{
			m_gliderAttachment->SetScale(Vector3(SpanScale(m_fold.GetUnfold()), 1.0f, 1.0f));
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
