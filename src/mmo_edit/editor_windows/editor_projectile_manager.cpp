// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "editor_projectile_manager.h"
#include "audio/audio.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/entity.h"
#include "scene_graph/particle_emitter.h"
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

		// Update 3D sound position
		if (m_audio && m_soundChannel != InvalidChannel)
		{
			m_audio->Set3DPosition(m_soundChannel, m_node->GetDerivedPosition());
		}

		// Check for impact
		const Vector3 currentPos = m_node->GetDerivedPosition();
		const Vector3 targetPos = m_target->GetPosition();
		const float distanceToTarget = (targetPos - currentPos).GetLength();

		if (distanceToTarget <= 0.5f)
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
			direction.Normalize();
			m_node->Translate(direction * m_params.speed * deltaTime, TransformSpace::World);
		}
	}

	void EditorProjectile::UpdateArcMotion(float deltaTime)
	{
		if (!m_target)
		{
			return;
		}

		const Vector3 targetPos = m_target->GetPosition();
		const float travelProgress = (m_travelTime * m_params.speed) / m_totalDistance;

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
		m_velocity.Normalize();
		m_velocity = m_velocity.Lerp(desiredDirection, homingStrength * deltaTime);
		m_velocity.Normalize();
		m_velocity *= m_params.speed;

		// Move
		m_node->Translate(m_velocity * deltaTime, TransformSpace::World);
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

		// Calculate forward progress
		const float forwardDistance = m_travelTime * m_params.speed;
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
