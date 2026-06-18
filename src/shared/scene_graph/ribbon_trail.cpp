// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "ribbon_trail.h"
#include "camera.h"
#include "material_manager.h"
#include "render_operation.h"
#include "render_queue.h"
#include "scene.h"

#include <algorithm>

namespace mmo
{
	// Static member initialization
	const String RibbonTrail::TYPE_NAME = "RibbonTrail";

	// ========================================================================
	// RibbonTrailRenderable Implementation
	// ========================================================================

	RibbonTrailRenderable::RibbonTrailRenderable(GraphicsDevice& device, RibbonTrail& parent)
		: m_device(device)
		, m_parent(parent)
	{
		// Create vertex data container
		m_vertexData = std::make_unique<VertexData>(&m_device);

		// Set up vertex declaration for POS_COL_NORMAL_BINORMAL_TANGENT_TEX_VERTEX format
		size_t offset = 0;
		offset += m_vertexData->vertexDeclaration->AddElement(
			0, offset, VertexElementType::Float3, VertexElementSemantic::Position).GetSize();
		offset += m_vertexData->vertexDeclaration->AddElement(
			0, offset, VertexElementType::ColorArgb, VertexElementSemantic::Diffuse).GetSize();
		offset += m_vertexData->vertexDeclaration->AddElement(
			0, offset, VertexElementType::Float3, VertexElementSemantic::Normal).GetSize();
		offset += m_vertexData->vertexDeclaration->AddElement(
			0, offset, VertexElementType::Float3, VertexElementSemantic::Binormal).GetSize();
		offset += m_vertexData->vertexDeclaration->AddElement(
			0, offset, VertexElementType::Float3, VertexElementSemantic::Tangent).GetSize();
		offset += m_vertexData->vertexDeclaration->AddElement(
			0, offset, VertexElementType::Float2, VertexElementSemantic::TextureCoordinate).GetSize();

		// Create index data container
		m_indexData = std::make_unique<IndexData>();
	}

	void RibbonTrailRenderable::PrepareRenderOperation(RenderOperation& operation)
	{
		operation.topology = TopologyType::TriangleList;
		operation.vertexFormat = VertexFormat::PosColorNormalBinormalTangentTex1;
		operation.vertexData = m_vertexData.get();
		operation.indexData = m_indexData.get();
		operation.material = GetMaterial();
	}

	const Matrix4& RibbonTrailRenderable::GetWorldTransform() const
	{
		// Ribbon vertices are already in world space
		return Matrix4::Identity;
	}

	float RibbonTrailRenderable::GetSquaredViewDepth(const Camera& camera) const
	{
		// Use the parent's derived position for depth sorting
		return (m_parent.GetDerivedPosition() - camera.GetDerivedPosition()).GetSquaredLength();
	}

	MaterialPtr RibbonTrailRenderable::GetMaterial() const
	{
		return m_parent.GetMaterial();
	}

	void RibbonTrailRenderable::RebuildBuffers(
		const std::deque<RibbonSegment>& segments,
		const Camera& camera,
		const RibbonTrailParameters& params)
	{
		const size_t segmentCount = segments.size();
		if (segmentCount < 2)
		{
			m_vertexData->vertexCount = 0;
			if (m_indexData->indexBuffer)
			{
				m_indexData->indexCount = 0;
			}
			return;
		}

		// Calculate required buffer sizes
		// Each segment creates 2 vertices (left and right edge of ribbon)
		const size_t vertexCount = segmentCount * 2;
		// Each pair of consecutive segments creates 2 triangles (6 indices)
		const size_t indexCount = (segmentCount - 1) * 6;

		// Ensure vertex buffer exists and has sufficient capacity
		const size_t requiredVertexSize = vertexCount * sizeof(POS_COL_NORMAL_BINORMAL_TANGENT_TEX_VERTEX);
		if (!m_vertexBuffer || m_vertexBuffer->GetVertexCount() * sizeof(POS_COL_NORMAL_BINORMAL_TANGENT_TEX_VERTEX) < requiredVertexSize)
		{
			// Create or resize vertex buffer with some extra capacity
			const size_t newCapacity = std::max(vertexCount, static_cast<size_t>(params.maxSegments * 2));
			m_vertexBuffer = m_device.CreateVertexBuffer(
				newCapacity,
				sizeof(POS_COL_NORMAL_BINORMAL_TANGENT_TEX_VERTEX),
				BufferUsage::DynamicWriteOnly,
				nullptr);
			m_vertexData->vertexBufferBinding->SetBinding(0, m_vertexBuffer);
		}

		// Ensure index buffer exists and has sufficient capacity
		EnsureIndexBuffer(segmentCount);

		// Map vertex buffer and fill with ribbon geometry
		auto* vertices = static_cast<POS_COL_NORMAL_BINORMAL_TANGENT_TEX_VERTEX*>(
			m_vertexBuffer->Map(LockOptions::Discard));

		// Get camera vectors for billboard calculation
		const Vector3 cameraPos = camera.GetDerivedPosition();

		// Calculate texture coordinate step
		const float texStep = params.textureRepeat / static_cast<float>(segmentCount - 1);

		for (size_t i = 0; i < segmentCount; ++i)
		{
			const RibbonSegment& segment = segments[i];

			// Calculate interpolation factor (0 at head, 1 at tail)
			const float t = segment.age / params.segmentLifetime;
			const float clampedT = std::clamp(t, 0.0f, 1.0f);

			// Interpolate width
			const float width = params.initialWidth + (params.finalWidth - params.initialWidth) * clampedT;
			const float halfWidth = width * 0.5f;

			// Interpolate color
			const Vector4 color = params.initialColor + (params.finalColor - params.initialColor) * clampedT;

			// Convert color from Vector4 (0-1) to packed uint32. The Diffuse vertex
			// element (ColorArgb) is uploaded in ABGR byte order on the GPU, matching
			// the convention used by the particle emitter, so Red goes in the low byte
			// and Blue in bits 16-23. Packing it the other way swaps Red and Blue.
			const uint32 colorArgb =
				(static_cast<uint32>(color.w * 255.0f) << 24) |  // Alpha
				(static_cast<uint32>(color.z * 255.0f) << 16) |  // Blue
				(static_cast<uint32>(color.y * 255.0f) << 8) |   // Green
				(static_cast<uint32>(color.x * 255.0f));         // Red

			// Calculate the ribbon direction
			Vector3 direction;
			if (i < segmentCount - 1)
			{
				direction = (segments[i + 1].position - segment.position);
				if (direction.GetSquaredLength() > 0.0001f)
				{
					direction.Normalize();
				}
				else
				{
					direction = segment.direction;
				}
			}
			else
			{
				direction = segment.direction;
			}

			// Calculate the perpendicular vector for the ribbon width
			Vector3 perpendicular;
			if (params.faceCamera)
			{
				// Billboard mode: perpendicular is cross of direction and vector to camera
				const Vector3 toCamera = (cameraPos - segment.position);
				perpendicular = direction.Cross(toCamera);
				if (perpendicular.GetSquaredLength() > 0.0001f)
				{
					perpendicular.Normalize();
				}
				else
				{
					// Fallback if direction points at camera
					perpendicular = Vector3::UnitY.Cross(direction);
					if (perpendicular.GetSquaredLength() > 0.0001f)
					{
						perpendicular.Normalize();
					}
					else
					{
						perpendicular = Vector3::UnitX;
					}
				}
			}
			else
			{
				// Fixed up vector mode: perpendicular is cross of direction and fixed up
				perpendicular = direction.Cross(params.fixedUpVector);
				if (perpendicular.GetSquaredLength() > 0.0001f)
				{
					perpendicular.Normalize();
				}
				else
				{
					perpendicular = Vector3::UnitX;
				}
			}

			// Calculate normal (pointing towards camera or fixed)
			Vector3 normal;
			if (params.faceCamera)
			{
				normal = (cameraPos - segment.position);
				if (normal.GetSquaredLength() > 0.0001f)
				{
					normal.Normalize();
				}
				else
				{
					normal = Vector3::UnitZ;
				}
			}
			else
			{
				normal = perpendicular.Cross(direction);
				if (normal.GetSquaredLength() > 0.0001f)
				{
					normal.Normalize();
				}
				else
				{
					normal = Vector3::UnitY;
				}
			}

			// Tangent and binormal for normal mapping
			const Vector3 tangent = direction;
			const Vector3 binormal = normal.Cross(tangent);

			// Calculate UV coordinates
			float u = static_cast<float>(i) * texStep;
			if (params.textureScrolling)
			{
				// Offset based on age for scrolling effect
				u += segment.age * 0.5f;
			}

			// Generate 2 vertices for this segment (left and right edges)
			const size_t baseIndex = i * 2;

			// Left vertex
			vertices[baseIndex].pos = segment.position - perpendicular * halfWidth;
			vertices[baseIndex].color = colorArgb;
			vertices[baseIndex].normal = normal;
			vertices[baseIndex].binormal = binormal;
			vertices[baseIndex].tangent = tangent;
			vertices[baseIndex].uv[0] = u;
			vertices[baseIndex].uv[1] = 0.0f;

			// Right vertex
			vertices[baseIndex + 1].pos = segment.position + perpendicular * halfWidth;
			vertices[baseIndex + 1].color = colorArgb;
			vertices[baseIndex + 1].normal = normal;
			vertices[baseIndex + 1].binormal = binormal;
			vertices[baseIndex + 1].tangent = tangent;
			vertices[baseIndex + 1].uv[0] = u;
			vertices[baseIndex + 1].uv[1] = 1.0f;
		}

		m_vertexBuffer->Unmap();

		// Update vertex/index data counts
		m_vertexData->vertexCount = static_cast<uint32>(vertexCount);
		m_vertexData->vertexStart = 0;
		m_indexData->indexCount = static_cast<uint32>(indexCount);
		m_indexData->indexStart = 0;
	}

	void RibbonTrailRenderable::EnsureIndexBuffer(size_t segmentCount)
	{
		if (segmentCount < 2)
		{
			return;
		}

		// Calculate required index count: (segmentCount - 1) * 6 indices
		const size_t requiredIndices = (segmentCount - 1) * 6;

		// Check if we need to create or resize the buffer
		if (m_indexBufferCapacity >= requiredIndices)
		{
			return;
		}

		// Create with extra capacity for growth
		const size_t newCapacity = std::max(requiredIndices, static_cast<size_t>((m_parent.GetParameters().maxSegments - 1) * 6));

		m_indexBuffer = m_device.CreateIndexBuffer(
			newCapacity,
			IndexBufferSize::Index_16,
			BufferUsage::DynamicWriteOnly,
			nullptr);

		// Fill index buffer with triangle strip pattern
		auto* indices = static_cast<uint16*>(m_indexBuffer->Map(LockOptions::Discard));

		// Generate indices for each quad (pair of segments)
		// Each quad is made of 2 triangles
		for (size_t i = 0; i < newCapacity / 6; ++i)
		{
			const uint16 baseVertex = static_cast<uint16>(i * 2);
			const size_t baseIndex = i * 6;

			// Vertices layout for each segment pair:
			// i*2+0 (left0), i*2+1 (right0), i*2+2 (left1), i*2+3 (right1)
			//
			// First triangle: left0, right0, left1
			indices[baseIndex + 0] = baseVertex + 0;
			indices[baseIndex + 1] = baseVertex + 1;
			indices[baseIndex + 2] = baseVertex + 2;

			// Second triangle: right0, right1, left1
			indices[baseIndex + 3] = baseVertex + 1;
			indices[baseIndex + 4] = baseVertex + 3;
			indices[baseIndex + 5] = baseVertex + 2;
		}

		m_indexBuffer->Unmap();

		// Update capacity and bind to index data
		m_indexBufferCapacity = newCapacity;
		m_indexData->indexBuffer = m_indexBuffer;
	}

	// ========================================================================
	// RibbonTrail Implementation
	// ========================================================================

	RibbonTrail::RibbonTrail(const String& name, GraphicsDevice& device)
		: m_device(device)
	{
		m_name = name;
		m_boundingBox.min = Vector3::Zero;
		m_boundingBox.max = Vector3::Zero;
		m_renderable = std::make_unique<RibbonTrailRenderable>(m_device, *this);
		m_lastUpdateTime = std::chrono::high_resolution_clock::now();
	}

	const String& RibbonTrail::GetMovableType() const
	{
		return TYPE_NAME;
	}

	const AABB& RibbonTrail::GetBoundingBox() const
	{
		return m_boundingBox;
	}

	float RibbonTrail::GetBoundingRadius() const
	{
		return m_boundingBox.GetExtents().GetLength();
	}

	void RibbonTrail::Update()
	{
		// Calculate delta time using self-timing
		const auto now = std::chrono::high_resolution_clock::now();
		const float deltaTime = std::chrono::duration<float>(now - m_lastUpdateTime).count();
		m_lastUpdateTime = now;

		// Clamp delta time to avoid issues with large time gaps
		const float clampedDeltaTime = std::min(deltaTime, 0.1f);

		// Update existing segments (age them)
		UpdateSegments(clampedDeltaTime);

		// Try to add new segment if playing
		if (m_isPlaying)
		{
			TryAddSegment(clampedDeltaTime);
		}

		// Update bounding box
		UpdateBoundingBox();

		// Rebuild GPU buffers if we have a camera
		if (m_scene && m_segments.size() >= 2)
		{
			Camera* camera = m_scene->GetCamera(0);
			if (camera)
			{
				m_renderable->RebuildBuffers(m_segments, *camera, m_parameters);
			}
		}
	}

	void RibbonTrail::PopulateRenderQueue(RenderQueue& queue)
	{
		// Only add to render queue if we have enough segments and a material
		if (m_segments.size() >= 2 && m_material && m_renderable->IsReady())
		{
			queue.AddRenderable(*m_renderable, GetRenderQueueGroup(), 0);
		}
	}

	void RibbonTrail::VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables)
	{
		if (m_renderable && m_renderable->IsReady())
		{
			visitor.Visit(*m_renderable, 0, false);
		}
	}

	void RibbonTrail::SetParameters(const RibbonTrailParameters& params)
	{
		m_parameters = params;
	}

	void RibbonTrail::Play()
	{
		m_isPlaying = true;
		m_lastUpdateTime = std::chrono::high_resolution_clock::now();
		m_lastPosition = GetDerivedPosition();
	}

	void RibbonTrail::Stop()
	{
		m_isPlaying = false;
	}

	void RibbonTrail::Reset()
	{
		m_segments.clear();
		m_timeSinceLastSegment = 0.0f;
		m_lastUpdateTime = std::chrono::high_resolution_clock::now();
		m_lastPosition = GetDerivedPosition();
	}

	Vector3 RibbonTrail::GetDerivedPosition() const
	{
		if (m_parentNode)
		{
			return m_parentNode->GetDerivedPosition();
		}
		return Vector3::Zero;
	}

	MaterialPtr RibbonTrail::GetDefaultMaterial(bool additive)
	{
		// Try to load a default ribbon material
		const String materialName = additive
			? "Materials/RibbonTrail_Additive.hmat"
			: "Materials/RibbonTrail_Alpha.hmat";

		try
		{
			return MaterialManager::Get().Load(materialName);
		}
		catch (...)
		{
			// If ribbon-specific material doesn't exist, try the particle material
			const String fallbackName = additive
				? "Materials/Particle_Additive.hmat"
				: "Materials/Particle_Alpha.hmat";

			try
			{
				return MaterialManager::Get().Load(fallbackName);
			}
			catch (...)
			{
				return nullptr;
			}
		}
	}

	void RibbonTrail::TryAddSegment(float deltaTime)
	{
		m_timeSinceLastSegment += deltaTime;

		// Check if it's time to add a new segment
		if (m_timeSinceLastSegment < m_parameters.segmentInterval)
		{
			return;
		}

		const Vector3 currentPosition = GetDerivedPosition();

		// Check minimum distance requirement
		const float distSquared = (currentPosition - m_lastPosition).GetSquaredLength();
		if (distSquared < m_parameters.minSegmentDistance * m_parameters.minSegmentDistance)
		{
			// Not moved enough, don't add segment
			return;
		}

		// Calculate direction from last position
		Vector3 direction = currentPosition - m_lastPosition;
		if (direction.GetSquaredLength() > 0.0001f)
		{
			direction.Normalize();
		}
		else
		{
			// Use existing direction if available
			if (!m_segments.empty())
			{
				direction = m_segments.front().direction;
			}
			else
			{
				direction = Vector3::UnitZ;
			}
		}

		// Create new segment at the front (newest segments first)
		RibbonSegment segment;
		segment.position = currentPosition;
		segment.direction = direction;
		segment.age = 0.0f;
		segment.createTime = std::chrono::high_resolution_clock::now();

		m_segments.push_front(segment);

		// Remove excess segments
		while (m_segments.size() > m_parameters.maxSegments)
		{
			m_segments.pop_back();
		}

		m_lastPosition = currentPosition;
		m_timeSinceLastSegment = 0.0f;
	}

	void RibbonTrail::UpdateSegments(float deltaTime)
	{
		// Age all segments
		for (auto& segment : m_segments)
		{
			segment.age += deltaTime;
		}

		// Remove segments that have exceeded their lifetime
		while (!m_segments.empty() && m_segments.back().age > m_parameters.segmentLifetime)
		{
			m_segments.pop_back();
		}
	}

	void RibbonTrail::UpdateBoundingBox()
	{
		if (m_segments.empty())
		{
			m_boundingBox.min = Vector3::Zero;
			m_boundingBox.max = Vector3::Zero;
			return;
		}

		// Find min/max of all segment positions, accounting for width
		Vector3 minPos = m_segments.front().position;
		Vector3 maxPos = m_segments.front().position;

		for (const auto& segment : m_segments)
		{
			minPos.x = std::min(minPos.x, segment.position.x);
			minPos.y = std::min(minPos.y, segment.position.y);
			minPos.z = std::min(minPos.z, segment.position.z);

			maxPos.x = std::max(maxPos.x, segment.position.x);
			maxPos.y = std::max(maxPos.y, segment.position.y);
			maxPos.z = std::max(maxPos.z, segment.position.z);
		}

		// Expand by maximum width
		const float maxWidth = std::max(m_parameters.initialWidth, m_parameters.finalWidth);
		const Vector3 expansion(maxWidth, maxWidth, maxWidth);

		m_boundingBox.min = minPos - expansion;
		m_boundingBox.max = maxPos + expansion;
	}
}
