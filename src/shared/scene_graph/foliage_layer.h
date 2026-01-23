// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "mesh.h"
#include "graphics/material.h"
#include "base/typedefs.h"
#include "math/vector3.h"

#include <memory>
#include <random>

namespace mmo
{
	/// @brief Settings that control how foliage instances are generated and rendered.
	struct FoliageLayerSettings
	{
		/// @brief The density of foliage instances per square unit.
		float density = 1.0f;

		/// @brief Minimum scale factor for foliage instances.
		float minScale = 0.8f;

		/// @brief Maximum scale factor for foliage instances.
		float maxScale = 1.2f;

		/// @brief Whether to align foliage instances to the surface normal.
		bool alignToNormal = false;

		/// @brief Maximum slope angle (in degrees) at which foliage can be placed.
		/// @details Slopes steeper than this will not have foliage. Range 0-90.
		float maxSlopeAngle = 45.0f;

		/// @brief Random rotation around the up axis.
		bool randomYawRotation = true;

		/// @brief Minimum height at which foliage can be placed.
		float minHeight = -10000.0f;

		/// @brief Maximum height at which foliage can be placed.
		float maxHeight = 10000.0f;

		/// @brief Distance at which foliage starts to fade out.
		float fadeStartDistance = 50.0f;

		/// @brief Distance at which foliage is completely culled.
		float fadeEndDistance = 100.0f;

		/// @brief Whether this layer casts shadows.
		bool castShadows = false;

		/// @brief Seed for random number generation. 0 means use time-based seed.
		uint32 randomSeed = 0;
	};

	/// @brief Represents a single type of foliage (mesh + material + settings).
	/// @details A foliage layer defines what mesh to render and how instances should be
	///          generated and displayed. Multiple layers can be combined in a Foliage object.
	class FoliageLayer
	{
	public:
		/// @brief Creates a new foliage layer with the specified mesh.
		/// @param name Unique name for this layer.
		/// @param mesh The mesh to use for foliage instances.
		explicit FoliageLayer(const String& name, MeshPtr mesh);

		/// @brief Creates a new foliage layer with a mesh and custom material.
		/// @param name Unique name for this layer.
		/// @param mesh The mesh to use for foliage instances.
		/// @param material The material to use for rendering.
		FoliageLayer(const String& name, MeshPtr mesh, MaterialPtr material);

		~FoliageLayer() = default;

	public:
		/// @brief Gets the unique name of this layer.
		[[nodiscard]] const String& GetName() const
		{
			return m_name;
		}

		/// @brief Gets the mesh used for foliage instances.
		[[nodiscard]] const MeshPtr& GetMesh() const
		{
			return m_mesh;
		}

		/// @brief Sets the mesh used for foliage instances.
		/// @param mesh The new mesh to use.
		void SetMesh(MeshPtr mesh);

		/// @brief Gets the material used for rendering.
		[[nodiscard]] const MaterialPtr& GetMaterial() const
		{
			return m_material;
		}

		/// @brief Sets the material used for rendering.
		/// @param material The new material to use.
		void SetMaterial(MaterialPtr material);

		/// @brief Gets the current layer settings.
		[[nodiscard]] const FoliageLayerSettings& GetSettings() const
		{
			return m_settings;
		}

		/// @brief Gets mutable access to layer settings.
		[[nodiscard]] FoliageLayerSettings& GetSettings()
		{
			return m_settings;
		}

		/// @brief Sets the layer settings.
		/// @param settings The new settings to apply.
		void SetSettings(const FoliageLayerSettings& settings);

		/// @brief Sets the density of foliage instances per square unit.
		/// @param density The new density value.
		void SetDensity(float density);

		/// @brief Sets the scale range for foliage instances.
		/// @param minScale Minimum scale factor.
		/// @param maxScale Maximum scale factor.
		void SetScaleRange(float minScale, float maxScale);

		/// @brief Sets the height range for foliage placement.
		/// @param minHeight Minimum height.
		/// @param maxHeight Maximum height.
		void SetHeightRange(float minHeight, float maxHeight);

		/// @brief Sets the fade distances for distance-based culling.
		/// @param startDistance Distance at which fading begins.
		/// @param endDistance Distance at which foliage is fully culled.
		void SetFadeDistances(float startDistance, float endDistance);

		/// @brief Checks if a given position and slope is valid for this layer.
		/// @param position The world position to check.
		/// @param slopeAngle The slope angle at the position in degrees.
		/// @return True if foliage can be placed at this location.
		[[nodiscard]] bool IsValidPlacement(const Vector3& position, float slopeAngle) const;

		/// @brief Generates a random scale value based on layer settings.
		/// @param rng Random number generator to use.
		/// @return A random scale value between minScale and maxScale.
		[[nodiscard]] float GenerateRandomScale(std::mt19937& rng) const;

		/// @brief Generates a random yaw rotation if enabled.
		/// @param rng Random number generator to use.
		/// @return A random rotation angle in radians, or 0 if disabled.
		[[nodiscard]] float GenerateRandomYaw(std::mt19937& rng) const;

		/// @brief Calculates the fade factor based on distance from camera.
		/// @param distance The distance from the camera.
		/// @return A fade factor between 0 (invisible) and 1 (fully visible).
		[[nodiscard]] float CalculateFadeFactor(float distance) const;

		/// @brief Gets whether this layer has been modified since last rebuild.
		[[nodiscard]] bool IsDirty() const
		{
			return m_dirty;
		}

		/// @brief Marks this layer as needing a rebuild.
		void MarkDirty()
		{
			m_dirty = true;
		}

		/// @brief Clears the dirty flag after a rebuild.
		void ClearDirty()
		{
			m_dirty = false;
		}

	private:
		String m_name;
		MeshPtr m_mesh;
		MaterialPtr m_material;
		FoliageLayerSettings m_settings;
		bool m_dirty = true;
	};

	using FoliageLayerPtr = std::shared_ptr<FoliageLayer>;
}
