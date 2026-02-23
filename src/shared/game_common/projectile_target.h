// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"
#include "math/vector4.h"

#include <memory>

namespace mmo
{
	/// @brief Interface for projectile targets.
	/// 
	/// This interface abstracts away the target for projectiles, allowing
	/// the projectile system to be used in both the game client (with GameUnitC)
	/// and the editor (with simple mock targets).
	class IProjectileTarget
	{
	public:
		/// @brief Virtual destructor.
		virtual ~IProjectileTarget() = default;

		/// @brief Gets the current position of the target.
		/// @return The target's world position.
		virtual Vector3 GetPosition() const = 0;

		/// @brief Gets the unique identifier of the target.
		/// @return The target's GUID.
		virtual uint64 GetGuid() const = 0;
	};

	/// @brief A simple static projectile target for editor/preview use.
	/// 
	/// This implementation provides a fixed position target that doesn't
	/// require the full GameUnitC infrastructure.
	class StaticProjectileTarget : public IProjectileTarget
	{
	public:
		/// @brief Constructor.
		/// @param position The target position.
		/// @param guid Optional identifier.
		explicit StaticProjectileTarget(const Vector3& position, uint64 guid = 0)
			: m_position(position)
			, m_guid(guid)
		{
		}

		/// @copydoc IProjectileTarget::GetPosition
		Vector3 GetPosition() const override
		{
			return m_position;
		}

		/// @copydoc IProjectileTarget::GetGuid
		uint64 GetGuid() const override
		{
			return m_guid;
		}

		/// @brief Updates the target position.
		/// @param position The new position.
		void SetPosition(const Vector3& position)
		{
			m_position = position;
		}

	private:
		Vector3 m_position;
		uint64 m_guid;
	};

	/// @brief Motion types for projectiles.
	enum class ProjectileMotionType : int32
	{
		Linear = 0,
		Arc = 1,
		Homing = 2,
		SineWave = 3
	};

	/// @brief Parameters for spawning a projectile without proto dependencies.
	/// 
	/// This struct allows spawning projectiles in contexts where the proto
	/// types are not available (e.g., the editor due to header guard conflicts).
	struct ProjectileParams
	{
		/// @brief Mesh file name for the projectile visual.
		String meshFile;
		/// @brief Particle system file for trail effects.
		String particleFile;
		/// @brief Projectile speed in units per second.
		float speed = 15.0f;
		/// @brief The type of motion for the projectile.
		ProjectileMotionType motionType = ProjectileMotionType::Linear;
		/// @brief Height of arc (for Arc motion type).
		float arcHeight = 0.0f;
		/// @brief Width of arc (for Arc motion type, horizontal component).
		float arcWidth = 0.0f;
		/// @brief Amplitude (for Sine wave motion type).
		float sineAmplitude = 0.5f;
		/// @brief Frequency (for Sine wave motion type).
		float sineFrequency = 2.0f;
		/// @brief Scale of the projectile mesh.
		float scale = 1.0f;
		/// @brief Whether projectile rotates to face movement direction.
		bool faceMovement = true;

		/// @brief Whether the projectile has a point light.
		bool hasLight = false;
		/// @brief Light color (RGB).
		Vector4 lightColor = Vector4(1.0f, 0.9f, 0.7f, 1.0f);
		/// @brief Light intensity.
		float lightIntensity = 1.0f;
		/// @brief Light range.
		float lightRange = 10.0f;
		/// @brief Light fade-in time in seconds.
		float lightFadeInTime = 0.3f;
		/// @brief Light fade-out time in seconds (used on destroy).
		float lightFadeOutTime = 0.5f;

		/// @brief Whether the projectile has a ribbon trail.
		bool hasRibbonTrail = false;
		/// @brief Ribbon trail material name (empty for default).
		String ribbonMaterial;
		/// @brief Ribbon trail initial width.
		float ribbonInitialWidth = 0.5f;
		/// @brief Ribbon trail final width.
		float ribbonFinalWidth = 0.0f;
		/// @brief Ribbon trail initial color (RGBA).
		Vector4 ribbonInitialColor = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
		/// @brief Ribbon trail final color (RGBA).
		Vector4 ribbonFinalColor = Vector4(1.0f, 1.0f, 1.0f, 0.0f);
		/// @brief Ribbon trail segment lifetime.
		float ribbonSegmentLifetime = 1.0f;
		/// @brief Ribbon trail max segments.
		uint32 ribbonMaxSegments = 64;

		/// @brief Spawn offset to the right of the cast direction (negative = left).
		float spawnOffsetRight = 0.0f;
		/// @brief Spawn offset upward from the cast position (negative = down).
		float spawnOffsetUp = 0.0f;
	};
}
