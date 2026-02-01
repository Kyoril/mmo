// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "movable_object.h"
#include "renderable.h"
#include "graphics/graphics_device.h"
#include "graphics/vertex_index_data.h"
#include "math/vector3.h"
#include "math/vector4.h"

#include <chrono>
#include <deque>
#include <vector>

namespace mmo
{
	class RibbonTrail;
	class Camera;
	class Scene;

	/**
	 * @struct RibbonTrailParameters
	 * @brief Contains all parameters that define a ribbon trail's behavior.
	 *
	 * This structure holds all the configuration data for a ribbon trail,
	 * including segment timing, width, color, and texture settings.
	 */
	struct RibbonTrailParameters
	{
		/// Maximum number of segments the ribbon can have
		uint32 maxSegments{ 64 };

		/// How often to add a new segment (in seconds)
		float segmentInterval{ 0.05f };

		/// How long each segment lives before fading (in seconds)
		float segmentLifetime{ 1.0f };

		/// Initial width of the ribbon at the emitter point
		float initialWidth{ 1.0f };

		/// Final width of the ribbon at the tail (0 = fade to nothing)
		float finalWidth{ 0.0f };

		/// Initial color of the ribbon at the emitter point (RGBA)
		Vector4 initialColor{ 1.0f, 1.0f, 1.0f, 1.0f };

		/// Final color of the ribbon at the tail
		Vector4 finalColor{ 1.0f, 1.0f, 1.0f, 0.0f };

		/// Whether the ribbon should face the camera (billboard-style)
		bool faceCamera{ true };

		/// Fixed up vector when not facing camera (for sword trails etc.)
		Vector3 fixedUpVector{ Vector3::UnitY };

		/// Texture coordinates repeat count along the ribbon length
		float textureRepeat{ 1.0f };

		/// Whether texture should scroll with the ribbon
		bool textureScrolling{ false };

		/// Minimum distance between emitter position and last segment to add new segment
		float minSegmentDistance{ 0.01f };
	};

	/**
	 * @struct RibbonSegment
	 * @brief Represents a single segment (control point) in the ribbon trail.
	 */
	struct RibbonSegment
	{
		/// World position of this segment
		Vector3 position;

		/// Direction the ribbon was moving when this segment was created
		Vector3 direction;

		/// Age of this segment in seconds
		float age{ 0.0f };

		/// Time when this segment was created
		std::chrono::high_resolution_clock::time_point createTime;
	};

	/**
	 * @class RibbonTrailRenderable
	 * @brief Renderable implementation for rendering ribbon trails as triangle strips.
	 *
	 * This class handles the GPU rendering of ribbon trails by generating
	 * ribbon geometry and managing vertex/index buffers.
	 */
	class RibbonTrailRenderable final : public Renderable
	{
	public:
		/**
		 * @brief Constructs a new ribbon trail renderable.
		 * @param device The graphics device used to create GPU resources.
		 * @param parent The parent ribbon trail that owns this renderable.
		 */
		RibbonTrailRenderable(GraphicsDevice& device, RibbonTrail& parent);

		/**
		 * @brief Destructor.
		 */
		~RibbonTrailRenderable() override = default;

	public:
		// Renderable interface implementation

		/**
		 * @brief Prepares the render operation for this renderable.
		 * @param operation The render operation to configure.
		 */
		void PrepareRenderOperation(RenderOperation& operation) override;

		/**
		 * @brief Gets the world transformation matrix for this renderable.
		 * @return The world transform matrix (identity, as positions are in world space).
		 */
		[[nodiscard]] const Matrix4& GetWorldTransform() const override;

		/**
		 * @brief Gets the squared distance from the camera for sorting.
		 * @param camera The camera to calculate distance from.
		 * @return The squared view depth.
		 */
		[[nodiscard]] float GetSquaredViewDepth(const Camera& camera) const override;

		/**
		 * @brief Gets the material used to render the ribbon.
		 * @return The material pointer.
		 */
		[[nodiscard]] MaterialPtr GetMaterial() const override;

	public:
		// Ribbon-specific methods

		/**
		 * @brief Rebuilds vertex and index buffers from current segment data.
		 *
		 * Generates ribbon geometry from segments, creating 2 vertices per
		 * segment positioned to form a ribbon. Uses dynamic buffers for efficient
		 * per-frame updates.
		 *
		 * @param segments The list of segments to render.
		 * @param camera The camera used to calculate billboard orientation.
		 * @param params The ribbon parameters for width/color interpolation.
		 */
		void RebuildBuffers(
			const std::deque<RibbonSegment>& segments,
			const Camera& camera,
			const RibbonTrailParameters& params);

		/**
		 * @brief Checks if the renderable is ready to render.
		 * @return True if vertex data has been initialized.
		 */
		[[nodiscard]] bool IsReady() const
		{
			return m_vertexData != nullptr;
		}

	private:
		/**
		 * @brief Creates the index buffer if it doesn't exist or needs resizing.
		 * @param segmentCount The number of segments to create indices for.
		 */
		void EnsureIndexBuffer(size_t segmentCount);

	private:
		GraphicsDevice& m_device;                    ///< Graphics device for creating GPU resources
		RibbonTrail& m_parent;                       ///< Parent ribbon trail
		VertexBufferPtr m_vertexBuffer;              ///< Dynamic vertex buffer for ribbon quads
		IndexBufferPtr m_indexBuffer;                ///< Index buffer for triangle strip
		std::unique_ptr<VertexData> m_vertexData;    ///< Vertex data descriptor
		std::unique_ptr<IndexData> m_indexData;      ///< Index data descriptor
		size_t m_indexBufferCapacity{ 0 };           ///< Current capacity of index buffer
	};

	/**
	 * @class RibbonTrail
	 * @brief Main ribbon trail class that manages segment lifecycle and rendering.
	 *
	 * This class inherits from MovableObject to integrate with the scene graph system.
	 * It manages segment spawning, aging, and rendering. Unlike particles, ribbon trails
	 * create connected geometry between successive positions.
	 *
	 * Typical use cases:
	 * - Weapon swing trails (sword swipes, etc.)
	 * - Spell projectile trails
	 * - Motion blur effects
	 * - Vehicle exhaust trails
	 */
	class RibbonTrail final : public MovableObject
	{
	public:
		/**
		 * @brief Constructs a new ribbon trail.
		 * @param name Unique name for this trail.
		 * @param device Graphics device for creating GPU resources.
		 */
		explicit RibbonTrail(const String& name, GraphicsDevice& device);

		/**
		 * @brief Destructor.
		 */
		~RibbonTrail() override = default;

	public:
		// MovableObject interface implementation

		/**
		 * @brief Gets the type name of this movable object.
		 * @return "RibbonTrail"
		 */
		[[nodiscard]] const String& GetMovableType() const override;

		/**
		 * @brief Gets the local bounding box containing all segments.
		 * @return The bounding box in local space.
		 */
		[[nodiscard]] const AABB& GetBoundingBox() const override;

		/**
		 * @brief Gets the bounding radius of all segments.
		 * @return The bounding sphere radius.
		 */
		[[nodiscard]] float GetBoundingRadius() const override;

		/**
		 * @brief Adds this trail's renderables to the render queue.
		 * @param queue The render queue to populate.
		 */
		void PopulateRenderQueue(RenderQueue& queue) override;

		/**
		 * @brief Visits all renderables owned by this trail.
		 * @param visitor The visitor to accept.
		 * @param debugRenderables Whether to include debug renderables.
		 */
		void VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables) override;

	public:
		// Ribbon trail interface

		/**
		 * @brief Updates the ribbon trail.
		 *
		 * Uses self-timing via std::chrono to calculate deltaTime since this is called
		 * from Scene::UpdateSceneGraph which provides no deltaTime parameter.
		 */
		void Update();

		/**
		 * @brief Sets the trail parameters.
		 * @param params The new parameters to use.
		 */
		void SetParameters(const RibbonTrailParameters& params);

		/**
		 * @brief Gets the current trail parameters.
		 * @return The trail parameters.
		 */
		[[nodiscard]] const RibbonTrailParameters& GetParameters() const
		{
			return m_parameters;
		}

		/**
		 * @brief Starts the ribbon trail emission.
		 */
		void Play();

		/**
		 * @brief Stops adding new segments (existing segments continue to fade).
		 */
		void Stop();

		/**
		 * @brief Resets the trail, clearing all segments.
		 */
		void Reset();

		/**
		 * @brief Checks if the trail is currently active.
		 * @return True if playing, false otherwise.
		 */
		[[nodiscard]] bool IsPlaying() const
		{
			return m_isPlaying;
		}

		/**
		 * @brief Sets the material to use for rendering the ribbon.
		 * @param material The material pointer.
		 */
		void SetMaterial(const MaterialPtr& material)
		{
			m_material = material;
		}

		/**
		 * @brief Gets the material used for rendering the ribbon.
		 * @return The material pointer.
		 */
		[[nodiscard]] MaterialPtr GetMaterial() const
		{
			return m_material;
		}

		/**
		 * @brief Gets the derived position for rendering calculations.
		 * @return The world space position of the trail emitter.
		 */
		[[nodiscard]] Vector3 GetDerivedPosition() const;

		/**
		 * @brief Gets a default ribbon trail material.
		 * @param additive If true, returns additive blend material; if false, returns alpha blend.
		 * @return Material pointer, or nullptr if material failed to load.
		 */
		[[nodiscard]] static MaterialPtr GetDefaultMaterial(bool additive = false);

		/**
		 * @brief Gets the current segments (for debugging/visualization).
		 * @return The segment list.
		 */
		[[nodiscard]] const std::deque<RibbonSegment>& GetSegments() const
		{
			return m_segments;
		}

	private:
		/**
		 * @brief Adds a new segment at the current position if conditions are met.
		 * @param deltaTime Time elapsed since last update.
		 */
		void TryAddSegment(float deltaTime);

		/**
		 * @brief Ages and removes old segments.
		 * @param deltaTime Time elapsed since last update.
		 */
		void UpdateSegments(float deltaTime);

		/**
		 * @brief Updates the bounding box to contain all segments.
		 */
		void UpdateBoundingBox();

	private:
		GraphicsDevice& m_device;                                         ///< Graphics device for GPU resources
		RibbonTrailParameters m_parameters;                               ///< Trail configuration
		std::deque<RibbonSegment> m_segments;                             ///< Active segments
		std::unique_ptr<RibbonTrailRenderable> m_renderable;              ///< Renderable for GPU rendering
		MaterialPtr m_material;                                           ///< Material for rendering
		float m_timeSinceLastSegment{ 0.0f };                             ///< Time accumulator for segment spawning
		bool m_isPlaying{ false };                                        ///< Whether trail is actively adding segments
		mutable AABB m_boundingBox;                                       ///< Bounding box containing all segments
		Vector3 m_lastPosition{ Vector3::Zero };                          ///< Last recorded position
		std::chrono::high_resolution_clock::time_point m_lastUpdateTime;  ///< Last update timestamp for self-timing

		static const String TYPE_NAME;                                    ///< "RibbonTrail"
	};
}
