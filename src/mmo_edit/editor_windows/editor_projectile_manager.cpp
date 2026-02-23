// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "editor_projectile_manager.h"
#include "audio/audio.h"

#include <algorithm>

#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/entity.h"
#include "scene_graph/particle_emitter.h"
#include "scene_graph/light.h"
#include "scene_graph/ribbon_trail.h"
#include "scene_graph/material_manager.h"
#include "base/macros.h"
#include "log/default_log_levels.h"
#include "math/quaternion.h"

#include <cmath>

namespace mmo
{
	// Helper to erase element by moving last element into its place
	template <typename T>
	static typename std::vector<T>::iterator EraseByMove(std::vector<T>& vec, typename std::vector<T>::iterator it)
	{
		if (vec.size() > 1 && it != vec.end() - 1)
		{
			*it = std::move(vec.back());
		}
		vec.pop_back();
		return it;
	}

	EditorProjectile::EditorProjectile(Scene& scene,
	                                   IAudio* audio,
	                                   const ProjectileParams& params,
	                                   const Vector3& startPosition,
	                                   std::shared_ptr<IProjectileTarget> target)
		: m_scene(scene)
		, m_audio(audio)
		, m_params(params)
		, m_target(target)
		, m_node(nullptr)
		, m_entity(nullptr)
		, m_trailEmitter(nullptr)
		, m_light(nullptr)
		, m_ribbonTrail(nullptr)
		, m_startPosition(startPosition)
		, m_velocity(Vector3::UnitZ)
		, m_travelTime(0.0f)
		, m_totalDistance(0.0f)
		, m_hasHit(false)
		, m_soundChannel(InvalidChannel)
	{
		// Create scene node for projectile
		m_node = m_scene.GetRootSceneNode().CreateChildSceneNode();
		m_node->SetPosition(startPosition);

		// Create mesh entity if specified
		if (!params.meshFile.empty())
		{
			static uint64 s_projectileId = 0;
			const String entityName = "EditorProjectile_" + std::to_string(s_projectileId++);
			m_entity = m_scene.CreateEntity(entityName, params.meshFile);

			if (m_entity)
			{
				m_node->AttachObject(*m_entity);
				m_node->SetScale(Vector3(params.scale, params.scale, params.scale));
			}
		}

		// Create trail particle emitter if specified
		if (!params.particleFile.empty())
		{
			static uint64 s_trailId = 0;
			const String trailName = "EditorProjectileTrail_" + std::to_string(s_trailId++);
			m_trailEmitter = m_scene.CreateParticleEmitter(trailName);

			if (m_trailEmitter)
			{
				m_node->AttachObject(*m_trailEmitter);
				m_trailEmitter->Play();
			}
		}

		// Create point light if specified
		if (params.hasLight)
		{
			static uint64 s_lightId = 0;
			const String lightName = "EditorProjectileLight_" + std::to_string(s_lightId++);
			m_light = &m_scene.CreateLight(lightName, LightType::Point);
			m_light->SetColor(params.lightColor);
			m_light->SetIntensity(params.lightFadeInTime > 0.0f ? 0.0f : params.lightIntensity);
			m_light->SetRange(params.lightRange);
			m_node->AttachObject(*m_light);
		}

		// Create ribbon trail if specified
		if (params.hasRibbonTrail)
		{
			static uint64 s_ribbonId = 0;
			const String ribbonName = "EditorProjectileRibbon_" + std::to_string(s_ribbonId++);
			m_ribbonTrail = m_scene.CreateRibbonTrail(ribbonName);

			if (m_ribbonTrail)
			{
				RibbonTrailParameters ribbonParams;
				ribbonParams.initialWidth = params.ribbonInitialWidth;
				ribbonParams.finalWidth = params.ribbonFinalWidth;
				ribbonParams.initialColor = params.ribbonInitialColor;
				ribbonParams.finalColor = params.ribbonFinalColor;
				ribbonParams.segmentLifetime = params.ribbonSegmentLifetime;
				ribbonParams.maxSegments = params.ribbonMaxSegments;
				m_ribbonTrail->SetParameters(ribbonParams);

				if (!params.ribbonMaterial.empty())
				{
					auto material = MaterialManager::Get().Load(params.ribbonMaterial);
					if (material)
					{
						m_ribbonTrail->SetMaterial(material);
					}
				}
				else
				{
					m_ribbonTrail->SetMaterial(RibbonTrail::GetDefaultMaterial(true));
				}

				m_node->AttachObject(*m_ribbonTrail);
				m_ribbonTrail->Play();
			}
		}

		// Calculate initial velocity direction
		if (m_target)
		{
			const Vector3 targetPos = m_target->GetPosition();
			m_velocity = targetPos - startPosition;
			m_totalDistance = m_velocity.GetLength();

			if (m_totalDistance > 0.0f)
			{
				m_velocity.Normalize();
				m_velocity *= params.speed;
			}
		}
	}

	EditorProjectile::~EditorProjectile()
	{
		CleanupVisuals();
	}

	void EditorProjectile::CleanupVisuals()
	{
		// Stop looping sound
		if (m_audio && m_soundChannel != InvalidChannel)
		{
			m_audio->StopSound(&m_soundChannel);
			m_soundChannel = InvalidChannel;
		}

		// Destroy ribbon trail
		if (m_ribbonTrail)
		{
			m_ribbonTrail->Stop();
			m_scene.DestroyRibbonTrail(*m_ribbonTrail);
			m_ribbonTrail = nullptr;
		}

		// Destroy light
		if (m_light)
		{
			m_scene.DestroyLight(*m_light);
			m_light = nullptr;
		}

		// Destroy trail emitter
		if (m_trailEmitter)
		{
			m_scene.DestroyParticleEmitter(*m_trailEmitter);
			m_trailEmitter = nullptr;
		}

		// Destroy entity
		if (m_entity)
		{
			m_scene.DestroyEntity(*m_entity);
			m_entity = nullptr;
		}

		// Destroy scene node
		if (m_node)
		{
			m_scene.DestroySceneNode(*m_node);
			m_node = nullptr;
		}
	}

	bool EditorProjectile::Update(float deltaTime)
	{
		if (m_hasHit || !m_target)
		{
			return true;
		}

		m_travelTime += deltaTime;

		// Update position based on motion type
		switch (m_params.motionType)
		{
		case ProjectileMotionType::Linear:
			UpdateLinearMotion(deltaTime);
			break;
		case ProjectileMotionType::Arc:
			UpdateArcMotion(deltaTime);
			break;
		case ProjectileMotionType::Homing:
			UpdateHomingMotion(deltaTime);
			break;
		case ProjectileMotionType::SineWave:
			UpdateSineWaveMotion(deltaTime);
			break;
		}

		// Update rotation
		UpdateRotation(deltaTime);

		// Update light fade-in
		if (m_light && m_params.lightFadeInTime > 0.0f)
		{
			const float currentIntensity = m_light->GetIntensity();
			if (currentIntensity < m_params.lightIntensity)
			{
				const float newIntensity = currentIntensity + (m_params.lightIntensity / m_params.lightFadeInTime) * deltaTime;
				m_light->SetIntensity(std::min(newIntensity, m_params.lightIntensity));
			}
		}

		// Update 3D sound position
		if (m_audio && m_soundChannel != InvalidChannel)
		{
			m_audio->Set3DPosition(m_soundChannel, m_node->GetDerivedPosition());
		}

		// Check for impact
		const Vector3 currentPos = m_node->GetDerivedPosition();
		const Vector3 targetPos = m_target->GetPosition();
		const float distanceToTarget = (targetPos - currentPos).GetLength();

		// Use a distance threshold that accounts for the step size at the current speed,
		// so high-speed or low-framerate projectiles don't oscillate past the target.
		const float stepSize = m_params.speed * deltaTime;
		const float hitThreshold = std::max(0.5f, stepSize * 1.1f);

		if (distanceToTarget <= hitThreshold)
		{
			m_hasHit = true;
			return true;
		}

		return false;
	}

	void EditorProjectile::UpdateLinearMotion(float deltaTime)
	{
		if (!m_target)
		{
			return;
		}

		const Vector3 targetPos = m_target->GetPosition();
		const Vector3 currentPos = m_node->GetDerivedPosition();
		Vector3 direction = targetPos - currentPos;

		const float distance = direction.GetLength();
		if (distance > 0.0f)
		{
			// Clamp step to remaining distance to prevent overshooting
			const float step = std::min(m_params.speed * deltaTime, distance);
			direction.Normalize();
			m_node->Translate(direction * step, TransformSpace::World);
		}
	}

	void EditorProjectile::UpdateArcMotion(float deltaTime)
	{
		if (!m_target)
		{
			return;
		}

		const Vector3 targetPos = m_target->GetPosition();
		const float travelProgress = std::min((m_travelTime * m_params.speed) / m_totalDistance, 1.0f);

		if (travelProgress >= 1.0f)
		{
			m_node->SetPosition(targetPos);
			return;
		}

		// Calculate arc position
		const Vector3 linearPos = m_startPosition + (targetPos - m_startPosition) * travelProgress;

		// Parabolic arc: height peaks at 50% progress
		const float heightOffset = m_params.arcHeight * 4.0f * travelProgress * (1.0f - travelProgress);
		const Vector3 arcPos = linearPos + Vector3(0.0f, heightOffset, 0.0f);

		m_node->SetPosition(arcPos);
	}

	void EditorProjectile::UpdateHomingMotion(float deltaTime)
	{
		if (!m_target)
		{
			return;
		}

		const Vector3 currentPos = m_node->GetDerivedPosition();
		const Vector3 targetPos = m_target->GetPosition();

		// Calculate desired direction
		Vector3 desiredDirection = targetPos - currentPos;
		desiredDirection.Normalize();

		// Use a fixed homing strength for editor preview
		constexpr float homingStrength = 5.0f;

		// Smoothly turn velocity toward target
		const float lerpFactor = std::min(homingStrength * deltaTime, 1.0f);
		m_velocity.Normalize();
		m_velocity = m_velocity.Lerp(desiredDirection, lerpFactor);
		m_velocity.Normalize();
		m_velocity *= m_params.speed;

		// Clamp step to remaining distance to prevent overshooting
		const float distToTarget = (targetPos - currentPos).GetLength();
		const float step = std::min(m_velocity.GetLength() * deltaTime, distToTarget);
		Vector3 moveDir = m_velocity;
		moveDir.Normalize();
		m_node->Translate(moveDir * step, TransformSpace::World);
	}

	void EditorProjectile::UpdateSineWaveMotion(float deltaTime)
	{
		if (!m_target)
		{
			return;
		}

		const Vector3 targetPos = m_target->GetPosition();
		Vector3 direction = targetPos - m_startPosition;
		direction.Normalize();

		// Calculate forward progress, clamped to total distance
		const float forwardDistance = std::min(m_travelTime * m_params.speed, m_totalDistance);
		const Vector3 forwardPos = m_startPosition + direction * forwardDistance;

		// Calculate perpendicular offset (sine wave)
		Vector3 right = direction.Cross(Vector3::UnitY);
		right.Normalize();
		const float sideOffset = m_params.sineAmplitude * std::sin(m_travelTime * m_params.sineFrequency * 6.28318f);

		const Vector3 finalPos = forwardPos + right * sideOffset;
		m_node->SetPosition(finalPos);
	}

	void EditorProjectile::UpdateRotation(float deltaTime)
	{
		// Face movement direction
		if (m_params.faceMovement && m_velocity.GetLength() > 0.001f)
		{
			Vector3 forward = m_velocity;
			forward.Normalize();
			const Vector3 up = Vector3::UnitY;
			Vector3 right = up.Cross(forward);
			right.Normalize();
			Vector3 correctedUp = forward.Cross(right);
			correctedUp.Normalize();

			// Build rotation from axes (assuming Z-forward, Y-up convention)
			Quaternion orientation;
			orientation.FromAxes(right, correctedUp, forward);
			m_node->SetOrientation(orientation);
		}
	}

	const Vector3& EditorProjectile::GetPosition() const
	{
		return m_node->GetDerivedPosition();
	}

	// EditorProjectileManager implementation

	EditorProjectileManager::EditorProjectileManager(Scene& scene, IAudio* audio)
		: m_scene(scene)
		, m_audio(audio)
	{
	}

	void EditorProjectileManager::SpawnProjectile(const ProjectileParams& params,
	                                              const Vector3& startPosition,
	                                              std::shared_ptr<IProjectileTarget> target)
	{
		if (!target)
		{
			return;
		}

		if (params.speed <= 0.0f)
		{
			return;
		}

		auto projectile = std::make_unique<EditorProjectile>(m_scene, m_audio, params, startPosition, target);
		m_projectiles.push_back(std::move(projectile));
	}

	void EditorProjectileManager::Update(float deltaTime)
	{
		auto it = m_projectiles.begin();
		while (it != m_projectiles.end())
		{
			EditorProjectile* projectile = it->get();
			if (projectile->Update(deltaTime))
			{
				// Projectile hit - trigger impact event
				auto target = projectile->GetTarget();
				projectileImpact(target.get());

				// Remove projectile
				it = EraseByMove(m_projectiles, it);
			}
			else
			{
				++it;
			}
		}
	}

	void EditorProjectileManager::Clear()
	{
		m_projectiles.clear();
	}
}
