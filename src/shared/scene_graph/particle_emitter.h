// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file particle_emitter.h
 *
 * @brief Defines the core data structures for the particle system.
 *
 * This file contains the fundamental types used throughout the particle system,
 * including particle data, emitter shapes, and emitter parameters. These structures
 * are designed to be cache-friendly and serialization-ready.
 */

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"
#include "math/vector4.h"
#include "math/aabb.h"
#include "graphics/color_curve.h"
#include "renderable.h"
#include "graphics/vertex_index_data.h"
#include "movable_object.h"

#include <chrono>
#include <cstddef>
#include <vector>
#include <memory>

namespace mmo
{
	class Camera;
	class GraphicsDevice;
	class ParticleEmitter;
	class RenderQueue;

	/**
	 * @struct Particle
	 * @brief Represents a single particle in the system.
	 *
	 * This structure is designed to be exactly 64 bytes (one cache line) for
	 * optimal CPU cache performance. All particle data is stored in a
	 * structure-of-arrays (SoA) friendly format.
	 */
	struct Particle
	{
		Vector3 position;          ///< Current position of the particle in world space
		float size;                ///< Current size of the particle

		Vector3 velocity;          ///< Current velocity vector
		float rotation;            ///< Current rotation angle in radians

		Vector4 color;             ///< Current color (RGBA, each component 0-1)

		float age;                 ///< Current age in seconds
		float lifetime;            ///< Total lifetime in seconds
		float angularVelocity;     ///< Rotation speed in radians per second
		uint32 spriteIndex;        ///< Current sprite index for sprite sheet animation

		/**
		 * @brief Default constructor.
		 */
		Particle() = default;
	};

	// Compile-time verification of particle size
	static_assert(sizeof(Particle) == 64, "Particle struct must be exactly 64 bytes for cache line alignment");

	/**
	 * @enum EmitterShape
	 * @brief Defines the shape from which particles are spawned.
	 *
	 * The emitter shape determines the spatial distribution of newly
	 * spawned particles.
	 */
	enum class EmitterShape : uint8
	{
		Point,    ///< Particles spawn from a single point
		Sphere,   ///< Particles spawn from within a sphere volume
		Box,      ///< Particles spawn from within a box volume
		Cone      ///< Particles spawn from within a cone volume
	};

	/**
	 * @struct ParticleEmitterParameters
	 * @brief Contains all parameters that define a particle emitter's behavior.
	 *
	 * This structure holds all the configuration data for a particle emitter,
	 * including spawn rates, shapes, physical properties, and visual attributes.
	 * All members are public to simplify serialization and editor access.
	 */
	struct ParticleEmitterParameters
	{
		// Spawn parameters

		/// Number of particles to spawn per second
		float spawnRate { 10.0f };

		/// Maximum number of particles that can exist simultaneously
		uint32 maxParticles { 100 };

		// Shape parameters

		/// Shape from which particles are spawned
		EmitterShape shape { EmitterShape::Point };

		/// Dimensions of the emitter shape (radius for sphere, half-extents for box, etc.)
		Vector3 shapeExtents { Vector3::Zero };

		// Lifetime parameters

		/// Minimum lifetime for spawned particles (in seconds)
		float minLifetime { 1.0f };

		/// Maximum lifetime for spawned particles (in seconds)
		float maxLifetime { 2.0f };

		// Velocity parameters

		/// Minimum initial velocity for spawned particles
		Vector3 minVelocity { Vector3(0.0f, 1.0f, 0.0f) };

		/// Maximum initial velocity for spawned particles
		Vector3 maxVelocity { Vector3(0.0f, 2.0f, 0.0f) };

		// Force parameters

		/// Constant acceleration applied to all particles (e.g., gravity)
		Vector3 gravity { Vector3(0.0f, -9.81f, 0.0f) };

		// Size parameters

		/// Initial size of particles when spawned
		float startSize { 1.0f };

		/// Final size of particles when they die (interpolated over lifetime)
		float endSize { 0.0f };

		// Color parameters

		/// Color animation curve over the particle's lifetime (0.0 = birth, 1.0 = death)
		ColorCurve colorOverLifetime;

		// Sprite sheet parameters

		/// Number of columns in the sprite sheet texture
		uint32 spriteSheetColumns { 1 };

		/// Number of rows in the sprite sheet texture
		uint32 spriteSheetRows { 1 };

		/// Whether to animate sprites over the particle's lifetime
		bool animateSprites { false };

		// Material parameters

		/// Name of the material to use for rendering particles
		String materialName;

		/**
		 * @brief Default constructor.
		 *
		 * Initializes all parameters to sensible default values.
		 */
		ParticleEmitterParameters()
		{
			// Initialize color curve with a default white-to-transparent gradient
			colorOverLifetime = ColorCurve(
				Vector4(1.0f, 1.0f, 1.0f, 1.0f),  // Start: opaque white
				Vector4(1.0f, 1.0f, 1.0f, 0.0f)   // End: transparent white
			);
		}
	};

	/**
	 * @class ParticleRenderable
	 * @brief Renderable implementation for rendering particles as billboards.
	 *
	 * This class handles the GPU rendering of particles by generating billboard
	 * geometry (camera-facing quads) and managing vertex/index buffers. It follows
	 * the pattern established by ManualRenderObject for dynamic geometry.
	 */
	class ParticleRenderable final : public Renderable
	{
	public:
		/**
		 * @brief Constructs a new particle renderable.
		 * @param device The graphics device used to create GPU resources.
		 * @param parent The parent particle emitter that owns this renderable.
		 */
		ParticleRenderable(GraphicsDevice& device, ParticleEmitter& parent);

		/**
		 * @brief Destructor.
		 */
		~ParticleRenderable() override = default;

	public:
		// Renderable interface implementation

		/**
		 * @brief Prepares the render operation for this renderable.
		 * @param operation The render operation to configure.
		 */
		void PrepareRenderOperation(RenderOperation& operation) override;

		/**
		 * @brief Gets the world transformation matrix for this renderable.
		 * @return The world transform matrix.
		 */
		[[nodiscard]] const Matrix4& GetWorldTransform() const override;

		/**
		 * @brief Gets the squared distance from the camera for sorting.
		 * @param camera The camera to calculate distance from.
		 * @return The squared view depth.
		 */
		[[nodiscard]] float GetSquaredViewDepth(const Camera& camera) const override;

		/**
		 * @brief Gets the material used to render particles.
		 * @return The material pointer.
		 */
		[[nodiscard]] MaterialPtr GetMaterial() const override;

	public:
		// Particle-specific methods

		/**
		 * @brief Rebuilds vertex and index buffers from current particle data.
		 *
		 * Generates billboard geometry for each particle, creating 4 vertices per
		 * particle positioned to face the camera. Uses dynamic buffers for efficient
		 * per-frame updates.
		 *
		 * @param particles The list of particles to render.
		 * @param camera The camera used to calculate billboard orientation.
		 */
		void RebuildBuffers(const std::vector<Particle>& particles, const Camera& camera);

		/**
		 * @brief Sorts particles back-to-front for correct alpha blending.
		 *
		 * Only sorts if the material uses alpha blending. Particles are sorted by
		 * their distance from the camera to ensure correct rendering order.
		 *
		 * @param particles The particle list to sort (modified in-place).
		 * @param camera The camera used to calculate distances.
		 */
		void SortParticles(std::vector<Particle>& particles, const Camera& camera) const;

		/**
		 * @brief Checks if the renderable is ready to render.
		 * @return True if vertex data has been initialized.
		 */
		[[nodiscard]] bool IsReady() const { return m_vertexData != nullptr; }

	private:
		/**
		 * @brief Creates the index buffer if it doesn't exist.
		 *
		 * The index buffer is shared and only created once, using the pattern
		 * [0,1,2, 2,3,0] repeated for each particle quad.
		 *
		 * @param particleCount The number of particles to create indices for.
		 */
		void EnsureIndexBuffer(size_t particleCount);

	private:
		GraphicsDevice& m_device;                    ///< Graphics device for creating GPU resources
		ParticleEmitter& m_parent;                   ///< Parent particle emitter
		VertexBufferPtr m_vertexBuffer;              ///< Dynamic vertex buffer for particle quads
		IndexBufferPtr m_indexBuffer;                ///< Shared index buffer for all quads
		std::unique_ptr<VertexData> m_vertexData;    ///< Vertex data descriptor
		std::unique_ptr<IndexData> m_indexData;      ///< Index data descriptor
		size_t m_indexBufferCapacity { 0 };          ///< Current capacity of index buffer
	};

	/**
	 * @class ParticleEmitter
	 * @brief Main particle emitter class that manages particle lifecycle and rendering.
	 *
	 * This class inherits from MovableObject to integrate with the scene graph system.
	 * It manages particle spawning, updating, and rendering using a self-timing mechanism
	 * (since Scene::UpdateSceneGraph provides no deltaTime parameter).
	 */
	class ParticleEmitter final : public MovableObject
	{
	public:
		/**
		 * @brief Constructs a new particle emitter.
		 * @param name Unique name for this emitter.
		 * @param device Graphics device for creating GPU resources.
		 */
		explicit ParticleEmitter(const String& name, GraphicsDevice& device);

		/**
		 * @brief Destructor.
		 */
		~ParticleEmitter() override = default;

	public:
		// MovableObject interface implementation

		/**
		 * @brief Gets the type name of this movable object.
		 * @return "ParticleEmitter"
		 */
		[[nodiscard]] const String& GetMovableType() const override;

		/**
		 * @brief Gets the local bounding box containing all particles.
		 * @return The bounding box in local space.
		 */
		[[nodiscard]] const AABB& GetBoundingBox() const override;

		/**
		 * @brief Gets the bounding radius of all particles.
		 * @return The bounding sphere radius.
		 */
		[[nodiscard]] float GetBoundingRadius() const override;

		/**
		 * @brief Adds this emitter's renderables to the render queue.
		 * @param queue The render queue to populate.
		 */
		void PopulateRenderQueue(RenderQueue& queue) override;

		/**
		 * @brief Visits all renderables owned by this emitter.
		 * @param visitor The visitor to accept.
		 * @param debugRenderables Whether to include debug renderables.
		 */
		void VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables) override;

	public:
		// Particle system interface

		/**
		 * @brief Updates the particle system.
		 *
		 * Uses self-timing via std::chrono to calculate deltaTime since this is called
		 * from Scene::UpdateSceneGraph which provides no deltaTime parameter.
		 */
		void Update();

		/**
		 * @brief Sets the emitter parameters.
		 * @param params The new parameters to use.
		 */
		void SetParameters(const ParticleEmitterParameters& params);

		/**
		 * @brief Gets the current emitter parameters.
		 * @return The emitter parameters.
		 */
		[[nodiscard]] const ParticleEmitterParameters& GetParameters() const { return m_parameters; }

		/**
		 * @brief Starts emitting particles.
		 */
		void Play();

		/**
		 * @brief Stops emitting new particles (existing particles continue to live).
		 */
		void Stop();

		/**
		 * @brief Resets the emitter, clearing all particles.
		 */
		void Reset();

		/**
		 * @brief Checks if the emitter is currently playing.
		 * @return True if playing, false otherwise.
		 */
		[[nodiscard]] bool IsPlaying() const { return m_isPlaying; }

		/**
		 * @brief Sets the material to use for rendering particles.
		 * @param material The material pointer.
		 */
		void SetMaterial(const MaterialPtr& material) { m_material = material; }

		/**
		 * @brief Gets the material used for rendering particles.
		 * @return The material pointer.
		 */
		[[nodiscard]] MaterialPtr GetMaterial() const { return m_material; }

		/**
		 * @brief Gets the derived position for rendering calculations.
		 * @return The world space position of the emitter.
		 */
		[[nodiscard]] Vector3 GetDerivedPosition() const;

		/**
		 * @brief Gets a default particle material.
		 * @param additive If true, returns additive blend material; if false, returns alpha blend material.
		 * @return Material pointer, or nullptr if material failed to load.
		 */
		[[nodiscard]] static MaterialPtr GetDefaultMaterial(bool additive = false);

	private:
		/**
		 * @brief Spawns new particles based on spawn rate and deltaTime.
		 * @param deltaTime Time elapsed since last spawn in seconds.
		 */
		void SpawnParticles(float deltaTime);

		/**
		 * @brief Updates all existing particles.
		 * @param deltaTime Time elapsed since last update in seconds.
		 */
		void UpdateParticles(float deltaTime);

		/**
		 * @brief Updates the bounding box to contain all particles.
		 */
		void UpdateBoundingBox();

		/**
		 * @brief Gets a random spawn position based on emitter shape.
		 * @return Position in local space.
		 */
		[[nodiscard]] Vector3 GetSpawnPosition() const;

		/**
		 * @brief Gets a random initial velocity within configured range.
		 * @return Velocity vector.
		 */
		[[nodiscard]] Vector3 GetInitialVelocity() const;

		/**
		 * @brief Gets a random value between min and max.
		 * @param min Minimum value.
		 * @param max Maximum value.
		 * @return Random value in range [min, max].
		 */
		[[nodiscard]] float RandomRange(float min, float max) const;

	private:
		GraphicsDevice& m_device;                                         ///< Graphics device for GPU resources
		ParticleEmitterParameters m_parameters;                           ///< Emitter configuration
		std::vector<Particle> m_particles;                                ///< Active particles
		std::unique_ptr<ParticleRenderable> m_renderable;                 ///< Renderable for GPU rendering
		MaterialPtr m_material;                                           ///< Material for rendering
		float m_spawnAccumulator { 0.0f };                                ///< Accumulator for fractional particle spawning
		bool m_isPlaying { false };                                       ///< Whether emitter is actively spawning
		mutable AABB m_boundingBox;                                       ///< Bounding box containing all particles
		std::chrono::high_resolution_clock::time_point m_lastUpdateTime;  ///< Last update timestamp for self-timing

		static const String TYPE_NAME;                                    ///< "ParticleEmitter"
	};
}
