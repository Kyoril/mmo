// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "projectile_manager.h"
#include "game_unit_c.h"
#include "object_mgr.h"

#include <algorithm>

#include "log/default_log_levels.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/entity.h"
#include "scene_graph/particle_emitter.h"
#include "scene_graph/light.h"
#include "scene_graph/ribbon_trail.h"
#include "scene_graph/material_manager.h"
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
						   const proto_client::ProjectileVisual *projectileVisual,
						   const Vector3 &startPosition,
						   std::shared_ptr<IProjectileTarget> target,
						   float animationDelay)
		: m_scene(scene), m_audio(audio), m_spell(spell), m_projectileVisual(projectileVisual), m_target(target), m_node(nullptr), m_entity(nullptr), m_trailEmitter(nullptr), m_light(nullptr), m_ribbonTrail(nullptr), m_lightTargetIntensity(0.0f), m_lightFadeInTime(0.0f), m_startPosition(startPosition), m_velocity(Vector3::UnitZ), m_travelTime(0.0f), m_totalDistance(0.0f), m_speedMultiplier(1.0f), m_hasHit(false), m_soundChannel(InvalidChannel)
	{
		// Create scene node for projectile
		m_node = m_scene.GetRootSceneNode().CreateChildSceneNode();
		m_node->SetPosition(startPosition);

		// Setup visual representation if configured
		if (m_projectileVisual)
		{
			const auto &projVis = *m_projectileVisual;

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

			// Create point light if specified
			if (projVis.has_light())
			{
				const auto& lightConfig = projVis.light();
				static uint64 s_projLightId = 0;
				const String lightName = "ProjectileLight_" + std::to_string(s_projLightId++);

				m_light = &m_scene.CreateLight(lightName, LightType::Point);
				m_light->SetColor(Vector4(lightConfig.r(), lightConfig.g(), lightConfig.b(), 1.0f));
				m_lightTargetIntensity = lightConfig.intensity();
				m_lightFadeInTime = lightConfig.fade_in_time();
				m_light->SetIntensity(m_lightFadeInTime > 0.0f ? 0.0f : m_lightTargetIntensity);
				m_light->SetRange(lightConfig.range());
				m_node->AttachObject(*m_light);
			}

			// Create ribbon trail if specified
			if (projVis.has_ribbon_trail())
			{
				const auto& trailConfig = projVis.ribbon_trail();
				static uint64 s_projRibbonId = 0;
				const String ribbonName = "ProjectileRibbon_" + std::to_string(s_projRibbonId++);

				m_ribbonTrail = m_scene.CreateRibbonTrail(ribbonName);
				if (m_ribbonTrail)
				{
					RibbonTrailParameters ribbonParams;
					ribbonParams.initialWidth = trailConfig.initial_width();
					ribbonParams.finalWidth = trailConfig.final_width();
					ribbonParams.initialColor = Vector4(trailConfig.initial_r(), trailConfig.initial_g(),
						trailConfig.initial_b(), trailConfig.initial_a());
					ribbonParams.finalColor = Vector4(trailConfig.final_r(), trailConfig.final_g(),
						trailConfig.final_b(), trailConfig.final_a());
					ribbonParams.segmentLifetime = trailConfig.segment_lifetime();
					ribbonParams.maxSegments = trailConfig.max_segments();
					m_ribbonTrail->SetParameters(ribbonParams);

					if (trailConfig.has_material_name() && !trailConfig.material_name().empty())
					{
						auto material = MaterialManager::Get().Load(trailConfig.material_name());
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

			// Boost speed to compensate for animation delay
			if (animationDelay > 0.0f && spell.speed() > 0.0f && m_totalDistance > 0.0f)
			{
				const float expectedTravelTime = m_totalDistance / spell.speed();
				const float remainingTime = expectedTravelTime - animationDelay;

				if (remainingTime > 0.1f)
				{
					// Scale speed so projectile arrives on time
					m_speedMultiplier = expectedTravelTime / remainingTime;
				}
				else
				{
					// Delay consumed most/all of the travel time — use a high speed cap
					m_speedMultiplier = expectedTravelTime / 0.1f;
				}

				// Apply multiplier to initial velocity
				m_velocity *= m_speedMultiplier;
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
		if (m_projectileVisual)
		{
			switch (m_projectileVisual->motion())
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

		// Update light fade-in
		if (m_light && m_lightFadeInTime > 0.0f)
		{
			const float currentIntensity = m_light->GetIntensity();
			if (currentIntensity < m_lightTargetIntensity)
			{
				const float newIntensity = currentIntensity + (m_lightTargetIntensity / m_lightFadeInTime) * deltaTime;
				m_light->SetIntensity(std::min(newIntensity, m_lightTargetIntensity));
			}
		}

		// Check for impact
		const Vector3 currentPos = m_node->GetDerivedPosition();
		const Vector3 targetPos = m_target->GetPosition();
		const float distanceToTarget = (targetPos - currentPos).GetLength();

		// Use a distance threshold that accounts for the step size at the current speed,
		// so high-speed or low-framerate projectiles don't oscillate past the target.
		const float stepSize = m_spell.speed() * m_speedMultiplier * deltaTime;
		const float hitThreshold = std::max(0.5f, stepSize * 1.1f);

		if (distanceToTarget <= hitThreshold)
		{
			m_hasHit = true;
			return true;
		}

		return false;
	}

	void Projectile::UpdateLinearMotion(float deltaTime)
	{
		// Simple linear movement toward target position
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
			const float step = std::min(m_spell.speed() * m_speedMultiplier * deltaTime, distance);
			direction.Normalize();
			m_node->Translate(direction * step, TransformSpace::World);
		}
	}

	void Projectile::UpdateArcMotion(float deltaTime)
	{
		if (!m_target)
		{
			return;
		}

		const Vector3 targetPos = m_target->GetPosition();
		const float travelProgress = std::min((m_travelTime * m_spell.speed() * m_speedMultiplier) / m_totalDistance, 1.0f);

		if (travelProgress >= 1.0f)
		{
			m_node->SetPosition(targetPos);
			return;
		}

		// Calculate arc position
		const Vector3 linearPos = m_startPosition + (targetPos - m_startPosition) * travelProgress;

		// Read arc parameters directly from the proto accessor which returns
		// the proto default (0.0) for unset fields.
		const float arcHeight = m_projectileVisual ? m_projectileVisual->arc_height() : 0.0f;
		const float arcWidth  = m_projectileVisual ? m_projectileVisual->arc_width()  : 0.0f;

		// Parabolic arc: peaks at 50% progress
		const float arcFactor = 4.0f * travelProgress * (1.0f - travelProgress);
		const float heightOffset = arcHeight * arcFactor;

		// Horizontal arc offset (perpendicular to travel direction)
		if (arcWidth != 0.0f)
		{
			Vector3 travelDir = targetPos - m_startPosition;
			travelDir.Normalize();
			Vector3 right = travelDir.Cross(Vector3::UnitY);
			if (right.GetLength() < 0.001f)
			{
				right = travelDir.Cross(Vector3::UnitX);
			}
			right.Normalize();
			const float widthOffset = arcWidth * arcFactor;
			const Vector3 arcPos = linearPos + Vector3(0.0f, heightOffset, 0.0f) + right * widthOffset;
			m_node->SetPosition(arcPos);
		}
		else
		{
			const Vector3 arcPos = linearPos + Vector3(0.0f, heightOffset, 0.0f);
			m_node->SetPosition(arcPos);
		}
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

		// Get homing strength (turn rate) – proto default is 5.0
		const float homingStrength = m_projectileVisual ? m_projectileVisual->homing_strength() : 5.0f;

		// Smoothly turn velocity toward target
		const float lerpFactor = std::min(homingStrength * deltaTime, 1.0f);
		m_velocity.Normalize();
		m_velocity = m_velocity.Lerp(desiredDirection, lerpFactor);
		m_velocity.Normalize();
		m_velocity *= m_spell.speed() * m_speedMultiplier;

		// Clamp step to remaining distance to prevent overshooting
		const float distToTarget = (targetPos - currentPos).GetLength();
		const float step = std::min(m_velocity.GetLength() * deltaTime, distToTarget);
		Vector3 moveDir = m_velocity;
		moveDir.Normalize();
		m_node->Translate(moveDir * step, TransformSpace::World);
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

		// Get wave parameters – proto defaults are 1.0 for both
		const float frequency = m_projectileVisual ? m_projectileVisual->wave_frequency() : 1.0f;
		const float amplitude = m_projectileVisual ? m_projectileVisual->wave_amplitude() : 1.0f;

		// Calculate forward progress, clamped to total distance
		const float forwardDistance = std::min(m_travelTime * m_spell.speed() * m_speedMultiplier, m_totalDistance);
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
		if (!m_projectileVisual)
		{
			return;
		}

		const auto &projVis = *m_projectileVisual;

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

	/// @brief Compute offset start position for a projectile based on spawn_offset_right and spawn_offset_up.
	static Vector3 ApplySpawnOffset(const Vector3 &startPosition, const Vector3 &targetPosition,
	                                float offsetRight, float offsetUp)
	{
		if (offsetRight == 0.0f && offsetUp == 0.0f)
		{
			return startPosition;
		}

		Vector3 forward = targetPosition - startPosition;
		forward.y = 0.0f;
		const float len = forward.GetLength();
		if (len < 0.001f)
		{
			return startPosition + Vector3(offsetRight, offsetUp, 0.0f);
		}

		forward /= len;
		Vector3 right = forward.Cross(Vector3::UnitY);
		right.Normalize();

		return startPosition + right * offsetRight + Vector3(0.0f, offsetUp, 0.0f);
	}

	/// @brief Collect the list of projectile visuals to spawn from a visualization.
	/// Uses the repeated 'projectiles' field if non-empty, else falls back to singular 'projectile'.
	static std::vector<const proto_client::ProjectileVisual*> CollectProjectileVisuals(
		const proto_client::SpellVisualization *visualization)
	{
		std::vector<const proto_client::ProjectileVisual*> result;
		if (!visualization)
		{
			return result;
		}

		if (visualization->projectiles_size() > 0)
		{
			for (int i = 0; i < visualization->projectiles_size(); ++i)
			{
				result.push_back(&visualization->projectiles(i));
			}
		}
		else if (visualization->has_projectile())
		{
			result.push_back(&visualization->projectile());
		}

		return result;
	}

	void ProjectileManager::SpawnProjectile(const proto_client::SpellEntry &spell,
											const proto_client::SpellVisualization *visualization,
											GameUnitC *caster,
											GameUnitC *target,
											float animationDelay)
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

		const Vector3 baseStartPosition = caster->GetPosition();
		const Vector3 targetPosition = target->GetPosition();

		// Get weak_ptr from ObjectMgr since GameUnitC doesn't use shared_from_this
		std::shared_ptr<GameObjectC> targetShared = ObjectMgr::Get<GameObjectC>(target->GetGuid());
		std::weak_ptr<GameUnitC> targetWeak = std::static_pointer_cast<GameUnitC>(targetShared);
		auto targetWrapper = std::make_shared<GameUnitProjectileTarget>(targetWeak);

		const auto projVisuals = CollectProjectileVisuals(visualization);
		if (projVisuals.empty())
		{
			// No projectile config — spawn a single default projectile
			auto projectile = std::make_unique<Projectile>(m_scene, m_audio, spell, nullptr, baseStartPosition, targetWrapper, animationDelay);
			m_projectiles.push_back(std::move(projectile));
		}
		else
		{
			for (const auto *projVis : projVisuals)
			{
				const float offsetRight = projVis->has_spawn_offset_right() ? projVis->spawn_offset_right() : 0.0f;
				const float offsetUp = projVis->has_spawn_offset_up() ? projVis->spawn_offset_up() : 0.0f;
				const Vector3 startPos = ApplySpawnOffset(baseStartPosition, targetPosition, offsetRight, offsetUp);

				auto projectile = std::make_unique<Projectile>(m_scene, m_audio, spell, projVis, startPos, targetWrapper, animationDelay);
				m_projectiles.push_back(std::move(projectile));
			}
		}
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

		const Vector3 targetPosition = target->GetPosition();
		const auto projVisuals = CollectProjectileVisuals(visualization);

		if (projVisuals.empty())
		{
			auto projectile = std::make_unique<Projectile>(m_scene, m_audio, spell, nullptr, startPosition, target);
			m_projectiles.push_back(std::move(projectile));
		}
		else
		{
			for (const auto *projVis : projVisuals)
			{
				const float offsetRight = projVis->has_spawn_offset_right() ? projVis->spawn_offset_right() : 0.0f;
				const float offsetUp = projVis->has_spawn_offset_up() ? projVis->spawn_offset_up() : 0.0f;
				const Vector3 startPos = ApplySpawnOffset(startPosition, targetPosition, offsetRight, offsetUp);

				auto projectile = std::make_unique<Projectile>(m_scene, m_audio, spell, projVis, startPos, target);
				m_projectiles.push_back(std::move(projectile));
			}
		}
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
		projVis->set_arc_width(params.arcWidth);
		projVis->set_wave_amplitude(params.sineAmplitude);
		projVis->set_wave_frequency(params.sineFrequency);
		projVis->set_scale(params.scale);
		projVis->set_face_movement(params.faceMovement);
		projVis->set_spawn_offset_right(params.spawnOffsetRight);
		projVis->set_spawn_offset_up(params.spawnOffsetUp);

		// Populate light config if present
		if (params.hasLight)
		{
			auto* lightConfig = projVis->mutable_light();
			lightConfig->set_r(params.lightColor.x);
			lightConfig->set_g(params.lightColor.y);
			lightConfig->set_b(params.lightColor.z);
			lightConfig->set_intensity(params.lightIntensity);
			lightConfig->set_range(params.lightRange);
			lightConfig->set_fade_in_time(params.lightFadeInTime);
			lightConfig->set_fade_out_time(params.lightFadeOutTime);
		}

		// Populate ribbon trail config if present
		if (params.hasRibbonTrail)
		{
			auto* trailConfig = projVis->mutable_ribbon_trail();
			if (!params.ribbonMaterial.empty())
			{
				trailConfig->set_material_name(params.ribbonMaterial);
			}
			trailConfig->set_initial_width(params.ribbonInitialWidth);
			trailConfig->set_final_width(params.ribbonFinalWidth);
			trailConfig->set_initial_r(params.ribbonInitialColor.x);
			trailConfig->set_initial_g(params.ribbonInitialColor.y);
			trailConfig->set_initial_b(params.ribbonInitialColor.z);
			trailConfig->set_initial_a(params.ribbonInitialColor.w);
			trailConfig->set_final_r(params.ribbonFinalColor.x);
			trailConfig->set_final_g(params.ribbonFinalColor.y);
			trailConfig->set_final_b(params.ribbonFinalColor.z);
			trailConfig->set_final_a(params.ribbonFinalColor.w);
			trailConfig->set_segment_lifetime(params.ribbonSegmentLifetime);
			trailConfig->set_max_segments(params.ribbonMaxSegments);
		}

		// Store the temp objects in the internal storage (to keep them alive)
		m_tempStorage->spells.push_back(std::move(tempSpell));
		m_tempStorage->visualizations.push_back(std::move(tempVis));

		// Apply spawn offset
		const Vector3 targetPosition = target->GetPosition();
		const float offsetRight = params.spawnOffsetRight;
		const float offsetUp = params.spawnOffsetUp;
		const Vector3 finalStartPos = ApplySpawnOffset(startPosition, targetPosition, offsetRight, offsetUp);

		const auto *storedProjVis = &m_tempStorage->visualizations.back()->projectile();
		auto projectile = std::make_unique<Projectile>(m_scene, m_audio, 
			*m_tempStorage->spells.back(), storedProjVis, finalStartPos, target);
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
