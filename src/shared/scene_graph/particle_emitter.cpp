// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "particle_emitter.h"

#include "camera.h"
#include "movable_object.h"
#include "node.h"
#include "render_operation.h"
#include "graphics/graphics_device.h"
#include "graphics/vertex_format.h"
#include "graphics/vertex_declaration.h"
#include "math/matrix4.h"

#include <algorithm>
#include <cstring>

namespace mmo
{
	// Temporary stub for ParticleEmitter until Task 3
	// This allows ParticleRenderable to compile
	class ParticleEmitter : public MovableObject
	{
	public:
		explicit ParticleEmitter(const String& name) 
		{ 
			m_name = name; 
		}
		
		MaterialPtr GetMaterial() const { return nullptr; }
		
		// Provide position for rendering calculations
		Vector3 GetDerivedPosition() const 
		{ 
			if (m_parentNode)
			{
				return m_parentNode->GetDerivedPosition();
			}
			return Vector3::Zero;
		}
		
		// Provide required MovableObject overrides
		const String& GetMovableType() const override 
		{ 
			static const String type = "ParticleEmitter"; 
			return type; 
		}
		
		const AABB& GetBoundingBox() const override 
		{ 
			static const AABB box(Vector3::Zero, Vector3::Zero); 
			return box; 
		}
		
		float GetBoundingRadius() const override { return 0.0f; }
		
		void NotifyMoved() override {}
		
		void VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables = false) override {}
		
		void PopulateRenderQueue(RenderQueue& queue) override {}
	};

	// ========================================================================
	// ParticleRenderable Implementation
	// ========================================================================

	ParticleRenderable::ParticleRenderable(GraphicsDevice& device, ParticleEmitter& parent)
		: m_device(device)
		, m_parent(parent)
	{
		// Create vertex data container
		m_vertexData = std::make_unique<VertexData>(&m_device);
		
		// Set up vertex declaration for POS_COL_TEX_VERTEX format
		size_t offset = 0;
		offset += m_vertexData->vertexDeclaration->AddElement(
			0, offset, VertexElementType::Float3, VertexElementSemantic::Position).GetSize();
		offset += m_vertexData->vertexDeclaration->AddElement(
			0, offset, VertexElementType::ColorArgb, VertexElementSemantic::Diffuse).GetSize();
		offset += m_vertexData->vertexDeclaration->AddElement(
			0, offset, VertexElementType::Float2, VertexElementSemantic::TextureCoordinate).GetSize();

		// Create index data container
		m_indexData = std::make_unique<IndexData>();
	}

	void ParticleRenderable::PrepareRenderOperation(RenderOperation& operation)
	{
		// Set up render operation similar to ManualRenderObject pattern
		operation.topology = TopologyType::TriangleList;
		operation.vertexFormat = VertexFormat::PosColorTex1;
		operation.vertexData = m_vertexData.get();
		operation.indexData = m_indexData.get();
	}

	const Matrix4& ParticleRenderable::GetWorldTransform() const
	{
		// Particles are positioned in world space, so return identity transform
		// (the parent emitter's transform is already baked into particle positions)
		return Matrix4::Identity;
	}

	float ParticleRenderable::GetSquaredViewDepth(const Camera& camera) const
	{
		// Get the emitter's position for distance calculation
		const Vector3 emitterPos = m_parent.GetDerivedPosition();
		const Vector3 cameraPos = camera.GetDerivedPosition();
		const Vector3 diff = emitterPos - cameraPos;
		return diff.GetSquaredLength();
	}

	MaterialPtr ParticleRenderable::GetMaterial() const
	{
		// Material is managed by the parent emitter
		// This will be implemented when ParticleEmitter class is created
		return nullptr; // Placeholder - will be fixed in Task 3
	}

	void ParticleRenderable::RebuildBuffers(const std::vector<Particle>& particles, const Camera& camera)
	{
		const size_t particleCount = particles.size();
		if (particleCount == 0)
		{
			m_vertexData->vertexCount = 0;
			if (m_indexData->indexBuffer)
			{
				m_indexData->indexCount = 0;
			}
			return;
		}

		// Calculate required buffer sizes
		const size_t vertexCount = particleCount * 4; // 4 vertices per quad
		const size_t indexCount = particleCount * 6;  // 6 indices per quad (2 triangles)

		// Get camera orientation vectors for billboarding
		const Quaternion& cameraOrientation = camera.GetDerivedOrientation();
		const Vector3 right = cameraOrientation * Vector3::UnitX;
		const Vector3 up = cameraOrientation * Vector3::UnitY;

		// Ensure index buffer exists and has sufficient capacity
		EnsureIndexBuffer(particleCount);

		// Create or resize vertex buffer if needed
		const size_t requiredBufferSize = vertexCount;
		if (!m_vertexBuffer || m_vertexBuffer->GetVertexCount() < requiredBufferSize)
		{
			// Allocate with some headroom to avoid frequent reallocations
			size_t newSize = requiredBufferSize;
			if (m_vertexBuffer)
			{
				// Double the size to reduce reallocation frequency
				newSize = m_vertexBuffer->GetVertexCount() * 2;
				if (newSize < requiredBufferSize)
				{
					newSize = requiredBufferSize;
				}
			}

			m_vertexBuffer = m_device.CreateVertexBuffer(
				newSize, 
				sizeof(POS_COL_TEX_VERTEX), 
				BufferUsage::DynamicWriteOnlyDiscardable, 
				nullptr);

			// Bind vertex buffer
			m_vertexData->vertexBufferBinding->SetBinding(0, m_vertexBuffer);
		}

		// Map vertex buffer and fill with billboard data
		auto* vertices = static_cast<POS_COL_TEX_VERTEX*>(m_vertexBuffer->Map(LockOptions::Discard));

		for (size_t i = 0; i < particleCount; ++i)
		{
			const Particle& particle = particles[i];
			
			// Calculate billboard corner offsets
			const float halfSize = particle.size * 0.5f;
			const Vector3 rightOffset = right * halfSize;
			const Vector3 upOffset = up * halfSize;

			// Convert color from Vector4 (0-1) to uint32 ARGB format
			const uint32 color = 
				(static_cast<uint32>(particle.color.w * 255.0f) << 24) | // Alpha
				(static_cast<uint32>(particle.color.x * 255.0f) << 16) | // Red
				(static_cast<uint32>(particle.color.y * 255.0f) << 8)  | // Green
				(static_cast<uint32>(particle.color.z * 255.0f));        // Blue

			// Calculate sprite sheet UV coordinates if using sprite animation
			const uint32 totalSprites = 1; // Will be implemented in Task 5
			const uint32 spriteIndex = particle.spriteIndex % totalSprites;
			const float uMin = 0.0f; // Placeholder - sprite sheet support in Task 5
			const float uMax = 1.0f;
			const float vMin = 0.0f;
			const float vMax = 1.0f;

			// Generate 4 vertices for the billboard quad
			const size_t baseIndex = i * 4;

			// Bottom-left
			vertices[baseIndex + 0].pos = particle.position - rightOffset - upOffset;
			vertices[baseIndex + 0].color = color;
			vertices[baseIndex + 0].uv[0] = uMin;
			vertices[baseIndex + 0].uv[1] = vMax;

			// Bottom-right
			vertices[baseIndex + 1].pos = particle.position + rightOffset - upOffset;
			vertices[baseIndex + 1].color = color;
			vertices[baseIndex + 1].uv[0] = uMax;
			vertices[baseIndex + 1].uv[1] = vMax;

			// Top-right
			vertices[baseIndex + 2].pos = particle.position + rightOffset + upOffset;
			vertices[baseIndex + 2].color = color;
			vertices[baseIndex + 2].uv[0] = uMax;
			vertices[baseIndex + 2].uv[1] = vMin;

			// Top-left
			vertices[baseIndex + 3].pos = particle.position - rightOffset + upOffset;
			vertices[baseIndex + 3].color = color;
			vertices[baseIndex + 3].uv[0] = uMin;
			vertices[baseIndex + 3].uv[1] = vMin;
		}

		m_vertexBuffer->Unmap();

		// Update vertex/index data counts
		m_vertexData->vertexCount = static_cast<uint32>(vertexCount);
		m_vertexData->vertexStart = 0;
		m_indexData->indexCount = indexCount;
		m_indexData->indexStart = 0;
	}

	void ParticleRenderable::SortParticles(std::vector<Particle>& particles, const Camera& camera) const
	{
		// Only sort if we have particles to sort
		if (particles.empty())
		{
			return;
		}

		// TODO: Only sort if material requires alpha blending
		// For now, always sort back-to-front for correct transparency

		const Vector3 cameraPos = camera.GetDerivedPosition();

		// Sort particles back-to-front (farthest first) for proper alpha blending
		std::sort(particles.begin(), particles.end(), 
			[&cameraPos](const Particle& a, const Particle& b) -> bool
			{
				const float distA = (a.position - cameraPos).GetSquaredLength();
				const float distB = (b.position - cameraPos).GetSquaredLength();
				return distA > distB; // Farthest first
			});
	}

	void ParticleRenderable::EnsureIndexBuffer(size_t particleCount)
	{
		const size_t requiredIndexCount = particleCount * 6;

		// Check if we need to create or resize the index buffer
		if (!m_indexBuffer || m_indexBufferCapacity < requiredIndexCount)
		{
			// Calculate new capacity (with headroom to reduce reallocations)
			size_t newCapacity = requiredIndexCount;
			if (m_indexBuffer)
			{
				// Double the capacity
				newCapacity = m_indexBufferCapacity * 2;
				if (newCapacity < requiredIndexCount)
				{
					newCapacity = requiredIndexCount;
				}
			}

			// Create index buffer
			m_indexBuffer = m_device.CreateIndexBuffer(
				newCapacity,
				IndexBufferSize::Index_16,
				BufferUsage::StaticWriteOnly);

			// Map and fill with quad indices
			auto* indices = static_cast<uint16*>(m_indexBuffer->Map(LockOptions::Normal));

			for (size_t i = 0; i < newCapacity / 6; ++i)
			{
				const uint16 baseVertex = static_cast<uint16>(i * 4);
				const size_t baseIndex = i * 6;

				// First triangle (0, 1, 2)
				indices[baseIndex + 0] = baseVertex + 0;
				indices[baseIndex + 1] = baseVertex + 1;
				indices[baseIndex + 2] = baseVertex + 2;

				// Second triangle (2, 3, 0)
				indices[baseIndex + 3] = baseVertex + 2;
				indices[baseIndex + 4] = baseVertex + 3;
				indices[baseIndex + 5] = baseVertex + 0;
			}

			m_indexBuffer->Unmap();

			// Update capacity and bind to index data
			m_indexBufferCapacity = newCapacity;
			m_indexData->indexBuffer = m_indexBuffer;
		}
	}
}
