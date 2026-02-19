// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "projectile_manager.h"
#include "game_unit_c.h"
#include "object_mgr.h"
#include "log/default_log_levels.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/entity.h"
#include "scene_graph/particle_emitter.h"
#include "shared/client_data/proto_client/spells.pb.h"
#include "shared/client_data/proto_client/spell_visualizations.pb.h"
#include "shared/audio/audio.h"
#include "base/macros.h"

namespace mmo
{
	// Helper to erase element by moving last element into its place
	template <typename T>
	static typename std::vector<T>::iterator EraseByMove(std::vector<T> &vec, typename std::vector<T>::iterator it)
	{
		if (vec.size() > 1 && it != vec.end() - 1)
		{
			*it = std::move(vec.back());
		}
		vec.pop_back();
		return it;
	}

	Projectile::Projectile(Scene &scene,
						   IAudio *audio,
						   const proto_client::SpellEntry &spell,
						   const proto_client::SpellVisualization *visualization,
						   const Vector3 &startPosition,
						   std::shared_ptr<IProjectileTarget> target)
		: m_scene(scene), m_audio(audio), m_spell(spell), m_visualization(visualization), m_target(target), m_node(nullptr), m_entity(nullptr), m_trailEmitter(nullptr), m_startPosition(startPosition), m_velocity(Vector3::UnitZ), m_travelTime(0.0f), m_totalDistance(0.0f), m_hasHit(false), m_soundChannel(InvalidChannel)
	{
		// Create scene node for projectile
		m_node = m_scene.GetRootSceneNode().CreateChildSceneNode();
		m_node->SetPosition(startPosition);

		// Setup visual representation if configured
		if (m_visualization && m_visualization->has_projectile())
		{
			const auto &projVis = m_visualization->projectile();

			// Create mesh entity if specified
			if (projVis.has_mesh_name() && !projVis.mesh_name().empty())
			{
				static uint64 s_projectileId = 0;
				const String entityName = "Projectile_" + std::to_string(s_projectileId++);
				m_entity = m_scene.CreateEntity(entityName, projVis.mesh_name());

				if (m_entity)
				{
					m_node->AttachObject(*m_entity);

					// Apply scale
					if (projVis.has_scale())
					{
						m_node->SetScale(Vector3(projVis.scale(), projVis.scale(), projVis.scale()));
					}
				}
			}

			// Create trail particle emitter if specified
			if (projVis.has_trail_particle() && !projVis.trail_particle().empty())
			{
				static uint64 s_trailId = 0;
				const String trailName = "ProjectileTrail_" + std::to_string(s_trailId++);
				m_trailEmitter = m_scene.CreateParticleEmitter(trailName);

				if (m_trailEmitter)
				{
					m_node->AttachObject(*m_trailEmitter);
					// TODO: Load particle parameters from data
					m_trailEmitter->Play();
				}
			}

			// Play flight sounds if specified
			if (m_audio && projVis.sounds_size() > 0)
			{
				const String &soundName = projVis.sounds(0);
				SoundIndex soundIdx = m_audio->FindSound(soundName, SoundType::SoundLooped3D);
				if (soundIdx == InvalidSound)
				{
					soundIdx = m_audio->CreateSound(soundName, SoundType::SoundLooped3D);
				}

				if (soundIdx != InvalidSound)
				{
					m_audio->PlaySound(soundIdx, &m_soundChannel);
					if (m_soundChannel != InvalidChannel)
					{
						m_audio->Set3DPosition(m_soundChannel, startPosition);
						m_audio->Set3DMinMaxDistance(m_soundChannel, 5.0f, 30.0f);
					}
				}
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
				m_velocity *= spell.speed();
			}
		}
	}

	Projectile::~Projectile()
	{
		CleanupVisuals();
	}

	void Projectile::CleanupVisuals()
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

	bool Projectile::Update(float deltaTime)
	{
		if (m_hasHit)
		{
			return true;
		}

		// Check if target still exists
		if (!m_target)
		{
			m_hasHit = true;
			return true;
		}

		m_travelTime += deltaTime;

		// Determine motion type and update position
		if (m_visualization && m_visualization->has_projectile())
		{
			const auto &projVis = m_visualization->projectile();
			switch (projVis.motion())
			{
			case proto_client::LINEAR:
				UpdateLinearMotion(deltaTime);
				break;
			case proto_client::ARC:
				UpdateArcMotion(deltaTime);
				break;
			case proto_client::HOMING:
				UpdateHomingMotion(deltaTime);
				break;
			case proto_client::SINE_WAVE:
				UpdateSineWaveMotion(deltaTime);
				break;
			default:
				UpdateLinearMotion(deltaTime);
				break;
			}

			// Update rotation
			UpdateRotation(deltaTime);
		}
		else
		{
			// Default linear motion
			UpdateLinearMotion(deltaTime);
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

		if (distanceToTarget <= 0.5f)
		{
			m_hasHit = true;
			return true;
		}

		return false;
	}

	void Projectile::UpdateLinearMotion(float deltaTime)
	{
		// Simple linear movement toward initial target position
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
			m_node->Translate(direction * m_spell.speed() * deltaTime, TransformSpace::World);
		}
	}

	void Projectile::UpdateArcMotion(float deltaTime)
	{
		if (!m_target)
		{
			return;
		}

		const Vector3 targetPos = m_target->GetPosition();
		const float travelProgress = (m_travelTime * m_spell.speed()) / m_totalDistance;

		if (travelProgress >= 1.0f)
		{
			m_node->SetPosition(targetPos);
			return;
		}

		// Calculate arc position
		const Vector3 linearPos = m_startPosition + (targetPos - m_startPosition) * travelProgress;

		float arcHeight = 5.0f; // Default arc height
		if (m_visualization && m_visualization->has_projectile() && m_visualization->projectile().has_arc_height())
		{
			arcHeight = m_visualization->projectile().arc_height();
		}

		// Parabolic arc: height peaks at 50% progress
		const float heightOffset = arcHeight * 4.0f * travelProgress * (1.0f - travelProgress);
		const Vector3 arcPos = linearPos + Vector3(0.0f, heightOffset, 0.0f);

		m_node->SetPosition(arcPos);
	}

	void Projectile::UpdateHomingMotion(float deltaTime)
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

		// Get homing strength (turn rate)
		float homingStrength = 5.0f;
		if (m_visualization && m_visualization->has_projectile() && m_visualization->projectile().has_homing_strength())
		{
			homingStrength = m_visualization->projectile().homing_strength();
		}

		// Smoothly turn velocity toward target
		m_velocity.Normalize();
		m_velocity = m_velocity.Lerp(desiredDirection, homingStrength * deltaTime);
		m_velocity.Normalize();
		m_velocity *= m_spell.speed();

		// Move
		m_node->Translate(m_velocity * deltaTime, TransformSpace::World);
	}

	void Projectile::UpdateSineWaveMotion(float deltaTime)
	{
		if (!m_target)
		{
			return;
		}

		const Vector3 targetPos = m_target->GetPosition();
		Vector3 direction = targetPos - m_startPosition;
		direction.Normalize();

		// Get wave parameters
		float frequency = 1.0f;
		float amplitude = 1.0f;
		if (m_visualization && m_visualization->has_projectile())
		{
			const auto &projVis = m_visualization->projectile();
			if (projVis.has_wave_frequency())
			{
				frequency = projVis.wave_frequency();
			}
			if (projVis.has_wave_amplitude())
			{
				amplitude = projVis.wave_amplitude();
			}
		}

		// Calculate forward progress
		const float forwardDistance = m_travelTime * m_spell.speed();
		const Vector3 forwardPos = m_startPosition + direction * forwardDistance;

		// Calculate perpendicular offset (sine wave)
		Vector3 right = direction.Cross(Vector3::UnitY);
		right.Normalize();
		const float sideOffset = amplitude * std::sin(m_travelTime * frequency * 6.28318f); // 2*PI

		const Vector3 finalPos = forwardPos + right * sideOffset;
		m_node->SetPosition(finalPos);
	}

	void Projectile::UpdateRotation(float deltaTime)
	{
		if (!m_visualization || !m_visualization->has_projectile())
		{
			return;
		}

		const auto &projVis = m_visualization->projectile();

		// Face movement direction
		if (projVis.has_face_movement() && projVis.face_movement())
		{
			if (m_velocity.GetLength() > 0.001f)
			{
				Vector3 forward = m_velocity;
				forward.Normalize();
				const Vector3 up = Vector3::UnitY;
				Vector3 right = up.Cross(forward);
				right.Normalize();
				Vector3 correctedUp = forward.Cross(right);
				correctedUp.Normalize();

				// Build rotation matrix (assuming Z-forward, Y-up convention)
				Quaternion orientation;
				orientation.FromAxes(right, correctedUp, forward);
				m_node->SetOrientation(orientation);
			}
		}

		// Apply spin
		if (projVis.has_spin_rate() && projVis.spin_rate() != 0.0f)
		{
			const float spinDegrees = projVis.spin_rate() * deltaTime;
			const float spinRadians = spinDegrees * 3.14159265f / 180.0f;
			const Quaternion spin(Radian(spinRadians), Vector3::UnitZ);
			m_node->Rotate(spin, TransformSpace::Local);
		}
	}

	const Vector3 &Projectile::GetPosition() const
	{
		return m_node->GetDerivedPosition();
	}

	// Internal storage for temporary proto objects created by SpawnProjectileEx
	struct ProjectileManager::TempProtoStorage
	{
		std::vector<std::unique_ptr<proto_client::SpellEntry>> spells;
		std::vector<std::unique_ptr<proto_client::SpellVisualization>> visualizations;
	};

	// ProjectileManager implementation

	ProjectileManager::ProjectileManager(Scene &scene, IAudio *audio)
		: m_scene(scene)
		, m_audio(audio)
		, m_tempStorage(std::make_unique<TempProtoStorage>())
	{
	}

	ProjectileManager::~ProjectileManager() = default;

	/// @brief Wrapper to adapt GameUnitC to IProjectileTarget interface.
	class GameUnitProjectileTarget : public IProjectileTarget
	{
	public:
		explicit GameUnitProjectileTarget(std::weak_ptr<GameUnitC> unit)
			: m_unit(unit)
		{
		}

		Vector3 GetPosition() const override
		{
			if (auto unit = m_unit.lock())
			{
				return unit->GetPosition();
			}
			return Vector3::Zero;
		}

		uint64 GetGuid() const override
		{
			if (auto unit = m_unit.lock())
			{
				return unit->GetGuid();
			}
			return 0;
		}

		/// @brief Check if the underlying unit is still valid.
		bool IsValid() const
		{
			return !m_unit.expired();
		}

	private:
		std::weak_ptr<GameUnitC> m_unit;
	};

	void ProjectileManager::SpawnProjectile(const proto_client::SpellEntry &spell,
											const proto_client::SpellVisualization *visualization,
											GameUnitC *caster,
											GameUnitC *target)
	{
		if (!caster || !target)
		{
			return;
		}

		// Only spawn projectiles for spells with speed > 0
		if (spell.speed() <= 0.0f)
		{
			return;
		}

		const Vector3 startPosition = caster->GetPosition();
		// Get weak_ptr from ObjectMgr since GameUnitC doesn't use shared_from_this
		std::shared_ptr<GameObjectC> targetShared = ObjectMgr::Get<GameObjectC>(target->GetGuid());
		std::weak_ptr<GameUnitC> targetWeak = std::static_pointer_cast<GameUnitC>(targetShared);

		auto targetWrapper = std::make_shared<GameUnitProjectileTarget>(targetWeak);
		auto projectile = std::make_unique<Projectile>(m_scene, m_audio, spell, visualization, startPosition, targetWrapper);
		m_projectiles.push_back(std::move(projectile));
	}

	void ProjectileManager::SpawnProjectile(const proto_client::SpellEntry &spell,
											const proto_client::SpellVisualization *visualization,
											const Vector3 &startPosition,
											std::shared_ptr<IProjectileTarget> target)
	{
		if (!target)
		{
			return;
		}

		// Only spawn projectiles for spells with speed > 0
		if (spell.speed() <= 0.0f)
		{
			return;
		}

		auto projectile = std::make_unique<Projectile>(m_scene, m_audio, spell, visualization, startPosition, target);
		m_projectiles.push_back(std::move(projectile));
	}

	void ProjectileManager::SpawnProjectileEx(const ProjectileParams& params,
	                                          const Vector3 &startPosition,
	                                          std::shared_ptr<IProjectileTarget> target)
	{
		if (!target)
		{
			return;
		}

		// Only spawn projectiles with speed > 0
		if (params.speed <= 0.0f)
		{
			return;
		}

		// Create temporary proto objects from params
		// These are stored in a helper structure that persists with the projectile
		auto tempSpell = std::make_unique<proto_client::SpellEntry>();
		tempSpell->set_id(0);
		tempSpell->set_speed(params.speed);

		auto tempVis = std::make_unique<proto_client::SpellVisualization>();
		auto* projVis = tempVis->mutable_projectile();
		
		if (!params.meshFile.empty())
		{
			projVis->set_mesh_name(params.meshFile);
		}
		
		if (!params.particleFile.empty())
		{
			projVis->set_trail_particle(params.particleFile);
		}
		
		projVis->set_motion(static_cast<proto_client::ProjectileMotion>(static_cast<int>(params.motionType)));
		projVis->set_arc_height(params.arcHeight);
		projVis->set_wave_amplitude(params.sineAmplitude);
		projVis->set_wave_frequency(params.sineFrequency);
		projVis->set_scale(params.scale);
		projVis->set_face_movement(params.faceMovement);

		// Store the temp objects in the internal storage (to keep them alive)
		m_tempStorage->spells.push_back(std::move(tempSpell));
		m_tempStorage->visualizations.push_back(std::move(tempVis));

		auto projectile = std::make_unique<Projectile>(m_scene, m_audio, 
			*m_tempStorage->spells.back(), m_tempStorage->visualizations.back().get(), startPosition, target);
		m_projectiles.push_back(std::move(projectile));
	}

	void ProjectileManager::Update(float deltaTime)
	{
		auto it = m_projectiles.begin();
		while (it != m_projectiles.end())
		{
			Projectile *projectile = it->get();
			if (projectile->Update(deltaTime))
			{
				// Projectile hit - trigger impact events
				auto target = projectile->GetTarget();
				projectileImpact(projectile->GetSpell(), target.get());
				projectileImpactSimple(target.get());

				// Remove projectile
				it = EraseByMove(m_projectiles, it);
			}
			else
			{
				++it;
			}
		}
	}

	void ProjectileManager::Clear()
	{
		m_projectiles.clear();
		if (m_tempStorage)
		{
			m_tempStorage->spells.clear();
			m_tempStorage->visualizations.clear();
		}
	}
}
