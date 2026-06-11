// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file particle_emitter.h
 *
 * @brief Core data structures and runtime classes for the particle system.
 *
 * The system follows an Unreal-Niagara-inspired model:
 *   - @ref ParticleSystem  is the scene object (a MovableObject) and owns one or more emitters.
 *   - @ref EmitterInstance is the simulation of a single emitter (its own particle pool + renderable).
 *   - @ref EmitterParameters is the serialized configuration of an emitter, grouped into
 *     "modules" (Emitter / Emission / Spawn / Update / Render) that the editor presents as a stack.
 *
 * For backward compatibility @c ParticleEmitter is a type alias of @ref ParticleSystem and
 * @c ParticleEmitterParameters is a type alias of @ref EmitterParameters.
 */

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"
#include "math/vector4.h"
#include "math/matrix4.h"
#include "math/aabb.h"
#include "graphics/color_curve.h"
#include "graphics/float_curve.h"
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
	class EmitterInstance;
	class ParticleSystem;
	class RenderQueue;
	class Mesh;
	class Material;

	/**
	 * @struct Particle
	 * @brief Represents a single particle in the system. Two cache lines (80 bytes).
	 */
	struct Particle
	{
		Vector3 position;          ///< Current position (world space, or emitter-local in Local sim space)
		float size;                ///< Current size (baseSize * sizeOverLife)

		Vector3 velocity;          ///< Current velocity vector
		float rotation;            ///< Current rotation angle in radians

		Vector4 color;             ///< Current color (RGBA, each component 0-1)

		float age;                 ///< Current age in seconds
		float lifetime;            ///< Total lifetime in seconds
		float angularVelocity;     ///< Rotation speed in radians per second
		uint32 spriteIndex;        ///< Current sprite frame index for sprite-sheet animation

		float baseSize;            ///< Randomized base size chosen at spawn (multiplied by the size curve)
		float randomPhase;         ///< Per-particle [0,1) value used for noise decorrelation / random start frame
		float pad0;                ///< Reserved
		float pad1;                ///< Reserved

		Particle() = default;
	};

	static_assert(sizeof(Particle) == 80, "Particle struct expected to be 80 bytes");

	/// @brief Shape from which particles are spawned.
	enum class EmitterShape : uint8
	{
		Point,    ///< Single point
		Sphere,   ///< Within a sphere volume
		Box,      ///< Within a box volume
		Cone      ///< Within a cone volume
	};

	/// @brief Space in which particles are simulated.
	enum class SimulationSpace : uint8
	{
		/// @brief Particles live in world space; once spawned they ignore emitter movement (trails, smoke).
		World,
		/// @brief Particles live in emitter-local space and follow the emitter (auras on a moving hand).
		Local
	};

	/// @brief How a particle quad is oriented when built.
	enum class ParticleRenderMode : uint8
	{
		BillboardFacing,     ///< Always faces the camera (default)
		VelocityAligned,     ///< Faces camera but the quad's up axis aligns with the velocity
		Stretched,           ///< Velocity-aligned and stretched along velocity (slashes, streaks)
		HorizontalBillboard, ///< Lies flat on the XZ plane (ground decals, shockwaves)
		Mesh                 ///< Renders a 3D mesh per particle via GPU instancing (debris, icons, shards)
	};

	/// @brief Sprite-sheet animation mode.
	enum class SpriteAnimationMode : uint8
	{
		None,            ///< Always use frame 0
		AnimateOverLife, ///< Plays the sheet across the particle's lifetime
		RandomStatic     ///< Picks one random frame at spawn and keeps it
	};

	/// @brief A scheduled instantaneous spawn of a number of particles.
	struct EmitterBurst
	{
		float time { 0.0f };   ///< Time within the emitter cycle (seconds) at which the burst fires
		uint32 count { 10 };   ///< Number of particles spawned by the burst
	};

	/**
	 * @struct EmitterParameters
	 * @brief Full configuration of a single emitter (one "module stack").
	 */
	struct EmitterParameters
	{
		// --- Emitter module ---
		String name { "Emitter" };                       ///< Display name in the editor
		bool enabled { true };                           ///< Whether the emitter simulates / renders
		SimulationSpace simulationSpace { SimulationSpace::World };
		bool loop { true };                              ///< Loop forever, or run a single cycle (one-shot)
		float duration { 1.0f };                         ///< Length of one cycle in seconds (burst timing / one-shot)
		float startDelay { 0.0f };                       ///< Delay before the emitter starts emitting
		float warmupTime { 0.0f };                       ///< Pre-simulated time when (re)started, so it looks "already running"
		float inheritVelocity { 0.0f };                  ///< Fraction of the emitter's own velocity added to new particles

		// --- Emission module ---
		float spawnRate { 10.0f };                       ///< Continuous spawns per second
		uint32 maxParticles { 100 };                     ///< Hard cap on simultaneously alive particles
		std::vector<EmitterBurst> bursts;                ///< Instantaneous bursts within a cycle

		// --- Shape module ---
		EmitterShape shape { EmitterShape::Point };
		Vector3 shapeExtents { Vector3::Zero };          ///< radius (sphere), full extents (box), {angleRad,height,baseRadius} (cone)

		// --- Spawn module ---
		float minLifetime { 1.0f };
		float maxLifetime { 2.0f };
		Vector3 minVelocity { Vector3(0.0f, 1.0f, 0.0f) };
		Vector3 maxVelocity { Vector3(0.0f, 2.0f, 0.0f) };
		float minStartSpeed { 0.0f };                    ///< Extra speed along the shape's outward direction
		float maxStartSpeed { 0.0f };
		float minStartSize { 1.0f };
		float maxStartSize { 1.0f };
		float minStartRotation { 0.0f };                 ///< Radians
		float maxStartRotation { 0.0f };
		float minAngularVelocity { 0.0f };               ///< Radians/second
		float maxAngularVelocity { 0.0f };

		// --- Update / forces module ---
		Vector3 gravity { Vector3(0.0f, -9.81f, 0.0f) }; ///< Constant acceleration
		float drag { 0.0f };                             ///< Linear damping (per second); velocity *= (1 - drag*dt)
		float orbitalSpeed { 0.0f };                     ///< Swirl around the emitter's local Y axis (radians/second)
		float radialAcceleration { 0.0f };               ///< Outward (+) / inward (-) acceleration from the emitter center
		Vector3 attractorPosition { Vector3::Zero };     ///< Point attractor position (emitter-local)
		float attractorStrength { 0.0f };                ///< Acceleration toward the attractor
		float noiseAmplitude { 0.0f };                   ///< Turbulence/curl-noise strength
		float noiseFrequency { 1.0f };                   ///< Turbulence spatial frequency

		// --- Over-life curves ---
		FloatCurve sizeOverLife;                         ///< Size multiplier over normalized life (default flat 1.0)
		ColorCurve colorOverLifetime;                    ///< RGBA over normalized life

		// --- Sprite sheet ---
		uint32 spriteSheetColumns { 1 };
		uint32 spriteSheetRows { 1 };
		SpriteAnimationMode spriteAnimation { SpriteAnimationMode::None };
		float spriteAnimationFps { 0.0f };               ///< 0 => one full sheet cycle across the particle's lifetime

		// --- Render module ---
		ParticleRenderMode renderMode { ParticleRenderMode::BillboardFacing };
		float lengthScale { 1.0f };                      ///< Stretch factor along velocity for VelocityAligned / Stretched
		String materialName;                             ///< Material used to render this emitter's particles (billboard modes)

		// --- Mesh render module (renderMode == Mesh) ---
		String meshName;                                 ///< .hmsh rendered once per particle via GPU instancing

		EmitterParameters()
		{
			// Default size curve is a flat 1.0 multiplier (size driven by min/maxStartSize).
			sizeOverLife = FloatCurve(1.0f, 1.0f);
			// Default colour fades opaque white to transparent white.
			colorOverLifetime = ColorCurve(
				Vector4(1.0f, 1.0f, 1.0f, 1.0f),
				Vector4(1.0f, 1.0f, 1.0f, 0.0f));
		}
	};

	/// @brief Backward-compatible alias for the previous flat parameter struct.
	using ParticleEmitterParameters = EmitterParameters;

	/**
	 * @struct ParticleSystemParameters
	 * @brief Configuration of a whole system: a collection of emitters.
	 */
	struct ParticleSystemParameters
	{
		std::vector<EmitterParameters> emitters;

		ParticleSystemParameters() = default;
	};

	/**
	 * @class ParticleRenderable
	 * @brief Renders the particles of one @ref EmitterInstance as quads.
	 */
	class ParticleRenderable final : public Renderable
	{
	public:
		ParticleRenderable(GraphicsDevice& device, EmitterInstance& parent);
		~ParticleRenderable() override = default;

	public:
		void PrepareRenderOperation(RenderOperation& operation) override;
		[[nodiscard]] const Matrix4& GetWorldTransform() const override;
		[[nodiscard]] float GetSquaredViewDepth(const Camera& camera) const override;
		[[nodiscard]] MaterialPtr GetMaterial() const override;

	public:
		/// @brief Rebuilds the GPU buffers from current particle data for the given camera.
		void RebuildBuffers(const std::vector<Particle>& particles, const Camera& camera);

		/// @brief Sorts particles back-to-front relative to the camera (for alpha blending).
		void SortParticles(std::vector<Particle>& particles, const Camera& camera) const;

		[[nodiscard]] bool IsReady() const { return m_vertexData != nullptr && m_vertexData->vertexCount > 0; }

	private:
		void EnsureIndexBuffer(size_t particleCount);

	private:
		GraphicsDevice& m_device;
		EmitterInstance& m_parent;
		VertexBufferPtr m_vertexBuffer;
		IndexBufferPtr m_indexBuffer;
		std::unique_ptr<VertexData> m_vertexData;
		std::unique_ptr<IndexData> m_indexData;
		size_t m_indexBufferCapacity { 0 };
	};

	/**
	 * @struct ParticleMeshInstance
	 * @brief Per-particle instance data uploaded to the GPU when an emitter renders meshes.
	 *
	 * Layout matches the instanced vertex-shader input: a column-major world matrix at
	 * TEXCOORD8-11 followed by an RGBA tint at TEXCOORD12.
	 */
	struct ParticleMeshInstance
	{
		Matrix4 world;   ///< World transform (translate * rotate * uniform scale) for this particle
		Vector4 color;   ///< Per-particle tint (color-over-life), multiplied with the mesh vertex colour
	};

	static_assert(sizeof(ParticleMeshInstance) == 80, "ParticleMeshInstance must match the instanced VS input layout");

	/**
	 * @class ParticleMeshRenderable
	 * @brief Renders one submesh of an emitter's mesh once per particle using GPU instancing.
	 *
	 * The geometry (vertex/index data) is cloned from the source mesh's submesh; the per-frame
	 * instance buffer (transform + tint per live particle) is owned by the @ref EmitterInstance and
	 * shared across all of its submesh renderables.
	 */
	class ParticleMeshRenderable final : public Renderable
	{
	public:
		ParticleMeshRenderable(EmitterInstance& parent, VertexData* vertexData, IndexData* indexData,
			MaterialPtr material);
		~ParticleMeshRenderable() override = default;

	public:
		void PrepareRenderOperation(RenderOperation& operation) override;
		[[nodiscard]] const Matrix4& GetWorldTransform() const override;
		[[nodiscard]] float GetSquaredViewDepth(const Camera& camera) const override;
		[[nodiscard]] MaterialPtr GetMaterial() const override;

		[[nodiscard]] bool IsReady() const;

	private:
		EmitterInstance& m_parent;
		VertexData* m_vertexData;
		IndexData* m_indexData;
		MaterialPtr m_material;
	};

	/**
	 * @class EmitterInstance
	 * @brief Simulates a single emitter: owns its particle pool, material and renderable.
	 */
	class EmitterInstance final
	{
	public:
		EmitterInstance(GraphicsDevice& device, ParticleSystem& system);
		~EmitterInstance() = default;

	public:
		void SetParameters(const EmitterParameters& params);
		[[nodiscard]] const EmitterParameters& GetParameters() const { return m_parameters; }
		[[nodiscard]] EmitterParameters& GetParametersMutable() { return m_parameters; }

		void SetMaterial(const MaterialPtr& material) { m_material = material; }
		[[nodiscard]] MaterialPtr GetMaterial() const { return m_material; }

		/// @brief Advances the simulation.
		/// @param deltaTime Frame time in seconds.
		/// @param systemWorld World transform of the owning system's node.
		/// @param systemWorldPos World position of the owning system.
		/// @param systemVelocity World-space velocity of the owning system (for inheritance).
		/// @param camera Camera used for sorting and billboard orientation (may be null).
		void Update(float deltaTime, const Matrix4& systemWorld, const Vector3& systemWorldPos,
			const Vector3& systemVelocity, const Camera* camera);

		void Reset();
		void Play() { m_emitting = true; }
		void Stop() { m_emitting = false; }
		[[nodiscard]] bool IsEmitting() const { return m_emitting; }

		/// @brief True when a non-looping emitter has finished and all its particles are dead.
		[[nodiscard]] bool IsFinished() const;

		[[nodiscard]] const std::vector<Particle>& GetParticles() const { return m_particles; }
		[[nodiscard]] size_t GetParticleCount() const { return m_particles.size(); }

		[[nodiscard]] const Matrix4& GetWorldTransform() const { return m_renderTransform; }
		[[nodiscard]] Vector3 GetEmitterWorldPosition() const { return m_emitterWorldPos; }

		[[nodiscard]] ParticleRenderable* GetRenderable() const { return m_renderable.get(); }
		[[nodiscard]] const AABB& GetBoundingBox() const { return m_boundingBox; }

		/// @brief True when this emitter renders meshes (renderMode == Mesh) and has valid geometry.
		[[nodiscard]] bool IsMeshMode() const { return m_meshMode && !m_meshRenderables.empty(); }

		/// @brief The per-particle instance buffer (transform + tint), valid only in mesh mode.
		[[nodiscard]] VertexBuffer* GetInstanceBuffer() const { return m_instanceBuffer.get(); }

		/// @brief Number of live instances currently in the instance buffer.
		[[nodiscard]] uint32 GetInstanceCount() const { return m_instanceCount; }

		/// @brief The submesh renderables used when rendering meshes (one per source submesh).
		[[nodiscard]] const std::vector<std::unique_ptr<ParticleMeshRenderable>>& GetMeshRenderables() const { return m_meshRenderables; }

	private:
		void SpawnOne(const Matrix4& systemWorld, const Vector3& systemWorldPos, const Vector3& systemVelocity);
		void UpdateParticles(float deltaTime);
		void UpdateBoundingBox();
		[[nodiscard]] Vector3 GetSpawnPosition(Vector3& outDirection) const;
		void SimulateWarmup(const Matrix4& systemWorld, const Vector3& systemWorldPos, const Vector3& systemVelocity);

		/// @brief (Re)loads the source mesh and clones its submesh geometry for instanced rendering.
		void RebuildMeshResources();

		/// @brief Rebuilds the per-particle GPU instance buffer (transform + tint) from live particles.
		void RebuildInstanceBuffer(const Camera& camera);

	private:
		GraphicsDevice& m_device;
		ParticleSystem& m_system;
		EmitterParameters m_parameters;
		std::vector<Particle> m_particles;
		std::unique_ptr<ParticleRenderable> m_renderable;
		MaterialPtr m_material;

		float m_spawnAccumulator { 0.0f };
		float m_age { 0.0f };          ///< Total time since (re)start (drives one-shot completion)
		float m_cycleTime { 0.0f };    ///< Time within the current cycle (drives bursts / looping)
		float m_noiseTime { 0.0f };    ///< Accumulated time used to evolve the turbulence field
		std::vector<uint8> m_burstFired;
		bool m_emitting { true };
		bool m_warmedUp { false };

		Matrix4 m_renderTransform { Matrix4::Identity };
		Vector3 m_emitterWorldPos { Vector3::Zero };
		mutable AABB m_boundingBox;

		// --- Mesh rendering resources (renderMode == Mesh) ---
		bool m_meshMode { false };
		String m_loadedMeshName;                                       ///< Tracks which mesh is currently cloned
		std::shared_ptr<Mesh> m_mesh;                                  ///< Source mesh (keeps it alive)
		std::vector<std::unique_ptr<VertexData>> m_meshVertexData;     ///< Cloned per-submesh vertex data
		std::vector<std::unique_ptr<IndexData>> m_meshIndexData;       ///< Cloned per-submesh index data
		std::vector<std::unique_ptr<ParticleMeshRenderable>> m_meshRenderables; ///< One renderable per submesh
		VertexBufferPtr m_instanceBuffer;                              ///< Per-particle instance data
		size_t m_instanceBufferCapacity { 0 };
		uint32 m_instanceCount { 0 };
	};

	/**
	 * @class ParticleSystem
	 * @brief Scene object owning one or more @ref EmitterInstance objects.
	 *
	 * Replaces the former single-emitter @c ParticleEmitter; a backward-compatible alias is provided
	 * along with convenience methods that operate on the first emitter.
	 */
	class ParticleSystem final : public MovableObject
	{
	public:
		explicit ParticleSystem(const String& name, GraphicsDevice& device);
		~ParticleSystem() override = default;

	public:
		// MovableObject interface
		[[nodiscard]] const String& GetMovableType() const override;
		[[nodiscard]] const AABB& GetBoundingBox() const override;
		[[nodiscard]] float GetBoundingRadius() const override;
		void PopulateRenderQueue(RenderQueue& queue) override;
		void VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables) override;

	public:
		/// @brief Advances all emitters. Called once per frame by the scene.
		void Update(float deltaTime);

		/// @brief Self-timed overload kept for callers that don't have a frame delta.
		void Update();

		// --- Multi-emitter API ---
		void SetSystemParameters(const ParticleSystemParameters& params);
		[[nodiscard]] ParticleSystemParameters GetSystemParameters() const;
		[[nodiscard]] size_t GetEmitterCount() const { return m_emitters.size(); }
		[[nodiscard]] EmitterInstance* GetEmitter(size_t index) const;
		EmitterInstance* AddEmitter(const EmitterParameters& params = EmitterParameters());
		void RemoveEmitter(size_t index);
		void ClearEmitters();

		// --- Backward-compatible single-emitter convenience (operate on emitter 0) ---
		void SetParameters(const EmitterParameters& params);
		[[nodiscard]] const EmitterParameters& GetParameters() const;
		void SetMaterial(const MaterialPtr& material);
		[[nodiscard]] MaterialPtr GetMaterial() const;

		void Play();
		void Stop();
		void Reset();
		[[nodiscard]] bool IsPlaying() const { return m_isPlaying; }

		/// @brief True when every emitter has finished (used to auto-destroy one-shot effects).
		[[nodiscard]] bool IsFinished() const;

		/// @brief When false the scene will not auto-advance this system; the owner drives Update()
		/// manually (used by the particle editor for pause / sim-speed / scrubbing control).
		void SetAutoUpdate(bool autoUpdate) { m_autoUpdate = autoUpdate; }
		[[nodiscard]] bool IsAutoUpdate() const { return m_autoUpdate; }

		[[nodiscard]] Vector3 GetDerivedPosition() const;

		/// @brief Total live particle count across all emitters (editor stats).
		[[nodiscard]] size_t GetTotalParticleCount() const;

		/// @brief Gets a default particle material (additive or alpha).
		[[nodiscard]] static MaterialPtr GetDefaultMaterial(bool additive = false);

	private:
		void UpdateBoundingBox();

	private:
		GraphicsDevice& m_device;
		std::vector<std::unique_ptr<EmitterInstance>> m_emitters;
		bool m_isPlaying { false };
		bool m_autoUpdate { true };
		mutable AABB m_boundingBox;
		std::chrono::high_resolution_clock::time_point m_lastUpdateTime;
		Vector3 m_lastWorldPos { Vector3::Zero };
		bool m_hasLastWorldPos { false };

		static const String TYPE_NAME;
	};

	/// @brief Backward-compatible alias: the scene object used to be called ParticleEmitter.
	using ParticleEmitter = ParticleSystem;
}
