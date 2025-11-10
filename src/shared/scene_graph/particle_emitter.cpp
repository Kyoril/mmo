// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "particle_emitter.h"

#include "camera.h"
#include "movable_object.h"
#include "node.h"
#include "render_operation.h"
#include "render_queue.h"
#include "scene.h"
#include "graphics/graphics_device.h"
#include "graphics/vertex_format.h"
#include "graphics/vertex_declaration.h"
#include "math/matrix4.h"
#include "base/random.h"

#include <algorithm>
#include <chrono>
#include <random>

namespace mmo
{
	// Static member initialization
	const String ParticleEmitter::TYPE_NAME = "ParticleEmitter";

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

	// ========================================================================
	// ParticleEmitter Implementation
	// ========================================================================

	ParticleEmitter::ParticleEmitter(const String& name, GraphicsDevice& device)
		: m_device(device)
	{
		m_name = name;
		m_boundingBox.min = Vector3::Zero;
		m_boundingBox.max = Vector3::Zero;
		m_renderable = std::make_unique<ParticleRenderable>(m_device, *this);
		m_lastUpdateTime = std::chrono::high_resolution_clock::now();
		
		// Reserve space for particles to avoid frequent reallocations
		m_particles.reserve(m_parameters.maxParticles);
	}

	const String& ParticleEmitter::GetMovableType() const
	{
		return TYPE_NAME;
	}

	const AABB& ParticleEmitter::GetBoundingBox() const
	{
		return m_boundingBox;
	}

	float ParticleEmitter::GetBoundingRadius() const
	{
		return m_boundingBox.GetExtents().GetLength();
	}

	void ParticleEmitter::PopulateRenderQueue(RenderQueue& queue)
	{
		// Update particles before rendering
		Update();

		// Only add to render queue if we have particles and a material
		if (!m_particles.empty() && m_material && m_renderable->IsReady())
		{
			queue.AddRenderable(*m_renderable, GetRenderQueueGroup(), 0);
		}
	}

	void ParticleEmitter::VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables)
	{
		if (m_renderable && m_renderable->IsReady())
		{
			visitor.Visit(*m_renderable, 0, false);
		}
	}

	void ParticleEmitter::Update()
	{
		// Calculate deltaTime using self-timing
		const auto currentTime = std::chrono::high_resolution_clock::now();
		const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - m_lastUpdateTime);
		const float deltaTime = duration.count() / 1000000.0f; // Convert to seconds
		m_lastUpdateTime = currentTime;

		// Clamp deltaTime to avoid huge jumps (e.g., during debugging or long frame hitches)
		const float clampedDeltaTime = std::min(deltaTime, 0.1f);

		// Update particle system
		if (m_isPlaying)
		{
			SpawnParticles(clampedDeltaTime);
		}

		UpdateParticles(clampedDeltaTime);
		UpdateBoundingBox();

		// Update renderable buffers if we have particles and a camera
		if (!m_particles.empty() && m_scene)
		{
			// Get the active camera from the scene (we'll use the first camera for now)
			// In a real implementation, this would be the rendering camera
			Camera* camera = m_scene->GetCamera(0);
			if (camera)
			{
				// Sort particles for alpha blending
				m_renderable->SortParticles(m_particles, *camera);

				// Rebuild GPU buffers
				m_renderable->RebuildBuffers(m_particles, *camera);
			}
		}
	}

	void ParticleEmitter::SetParameters(const ParticleEmitterParameters& params)
	{
		m_parameters = params;
		
		// Reserve appropriate particle capacity
		m_particles.reserve(params.maxParticles);
		
		// Trim excess particles if maxParticles was reduced
		if (m_particles.size() > params.maxParticles)
		{
			m_particles.resize(params.maxParticles);
		}
	}

	void ParticleEmitter::Play()
	{
		m_isPlaying = true;
	}

	void ParticleEmitter::Stop()
	{
		m_isPlaying = false;
	}

	void ParticleEmitter::Reset()
	{
		m_particles.clear();
		m_spawnAccumulator = 0.0f;
		m_lastUpdateTime = std::chrono::high_resolution_clock::now();
	}

	Vector3 ParticleEmitter::GetDerivedPosition() const
	{
		if (m_parentNode)
		{
			return m_parentNode->GetDerivedPosition();
		}
		return Vector3::Zero;
	}

	void ParticleEmitter::SpawnParticles(float deltaTime)
	{
		// Accumulate fractional particles
		m_spawnAccumulator += deltaTime * m_parameters.spawnRate;

		// Spawn particles while we have accumulated enough and haven't hit the limit
		while (m_spawnAccumulator >= 1.0f && m_particles.size() < m_parameters.maxParticles)
		{
			Particle particle;
			
			// Get emitter world position
			const Vector3 emitterPos = GetDerivedPosition();
			
			// Set initial position (local spawn position + emitter world position)
			particle.position = emitterPos + GetSpawnPosition();
			
			// Set initial velocity
			particle.velocity = GetInitialVelocity();
			
			// Set lifetime
			particle.lifetime = RandomRange(m_parameters.minLifetime, m_parameters.maxLifetime);
			particle.age = 0.0f;
			
			// Set initial size
			particle.size = m_parameters.startSize;
			
			// Set initial color (from curve at t=0)
			const Vector4 startColor = m_parameters.colorOverLifetime.Evaluate(0.0f);
			particle.color = startColor;
			
			// Set rotation
			particle.rotation = 0.0f;
			particle.angularVelocity = 0.0f; // Can be randomized later
			
			// Set sprite index
			particle.spriteIndex = 0;
			
			// Add to particle list
			m_particles.push_back(particle);
			
			// Decrease accumulator
			m_spawnAccumulator -= 1.0f;
		}
	}

	void ParticleEmitter::UpdateParticles(float deltaTime)
	{
		// Update each particle
		for (auto& particle : m_particles)
		{
			// Age the particle
			particle.age += deltaTime;
			
			// Skip dead particles (will be removed below)
			if (particle.age >= particle.lifetime)
			{
				continue;
			}
			
			// Update position
			particle.position += particle.velocity * deltaTime;
			
			// Apply gravity
			particle.velocity += m_parameters.gravity * deltaTime;
			
			// Update rotation
			particle.rotation += particle.angularVelocity * deltaTime;
			
			// Interpolate size over lifetime
			const float t = particle.age / particle.lifetime;
			particle.size = m_parameters.startSize + (m_parameters.endSize - m_parameters.startSize) * t;
			
			// Update color from curve
			particle.color = m_parameters.colorOverLifetime.Evaluate(t);
			
			// TODO: Update sprite index for sprite sheet animation (Task 5)
		}
		
		// Remove dead particles (erase-remove idiom)
		m_particles.erase(
			std::remove_if(m_particles.begin(), m_particles.end(),
				[](const Particle& p) { return p.age >= p.lifetime; }),
			m_particles.end());
	}

	void ParticleEmitter::UpdateBoundingBox()
	{
		if (m_particles.empty())
		{
			m_boundingBox.min = Vector3::Zero;
			m_boundingBox.max = Vector3::Zero;
			return;
		}

		// Find min and max extents of all particles
		Vector3 minPos = m_particles[0].position;
		Vector3 maxPos = m_particles[0].position;

		for (const auto& particle : m_particles)
		{
			// Account for particle size when calculating bounds
			const Vector3 halfSize(particle.size * 0.5f, particle.size * 0.5f, particle.size * 0.5f);
			
			const Vector3 pMin = particle.position - halfSize;
			const Vector3 pMax = particle.position + halfSize;
			
			minPos.x = std::min(minPos.x, pMin.x);
			minPos.y = std::min(minPos.y, pMin.y);
			minPos.z = std::min(minPos.z, pMin.z);
			
			maxPos.x = std::max(maxPos.x, pMax.x);
			maxPos.y = std::max(maxPos.y, pMax.y);
			maxPos.z = std::max(maxPos.z, pMax.z);
		}

		m_boundingBox.min = minPos;
		m_boundingBox.max = maxPos;
	}

	Vector3 ParticleEmitter::GetSpawnPosition() const
	{
		switch (m_parameters.shape)
		{
			case EmitterShape::Point:
				return Vector3::Zero;

		case EmitterShape::Sphere:
		{
			// Random point in sphere using rejection sampling
			// This method is more uniform than spherical coordinates
			const float radius = m_parameters.shapeExtents.x;
			Vector3 offset;
			do
			{
				offset = Vector3(
					RandomRange(-1.0f, 1.0f),
					RandomRange(-1.0f, 1.0f),
					RandomRange(-1.0f, 1.0f)
				);
			} while (offset.GetSquaredLength() > 1.0f);
			
			return offset * radius;
		}

		case EmitterShape::Box:
		{
			// Random point in box
			// shapeExtents represents full box dimensions, so use half extents
			const Vector3 halfExtents = m_parameters.shapeExtents * 0.5f;
			return Vector3(
				RandomRange(-halfExtents.x, halfExtents.x),
				RandomRange(-halfExtents.y, halfExtents.y),
				RandomRange(-halfExtents.z, halfExtents.z)
			);
		}

		case EmitterShape::Cone:
		{
			// Random point in cone
			// shapeExtents: x=angle (in radians), y=height, z=radius at base
			const float angle = m_parameters.shapeExtents.x;
			const float height = m_parameters.shapeExtents.y;
			const float baseRadius = m_parameters.shapeExtents.z;
			
			// Random position along cone height (0 = tip, 1 = base)
			const float t = RandomRange(0.0f, 1.0f);
			const float currentHeight = height * t;
			const float currentRadius = baseRadius * t;
			
			// Random angle around the cone axis
			const float theta = RandomRange(0.0f, 2.0f * 3.14159265f);
			
			// Generate point in cone space (Y-up)
			Vector3 offset(
				currentRadius * std::cos(theta),
				currentHeight,
				currentRadius * std::sin(theta)
			);
			
			// Apply parent node orientation if available
			if (m_parentNode)
			{
				const Quaternion& rot = m_parentNode->GetDerivedOrientation();
				return rot * offset;  // Rotate offset by node orientation
			}
			
			return offset;
		}

		default:
			return Vector3::Zero;
		}
	}

	Vector3 ParticleEmitter::GetInitialVelocity() const
	{
		return Vector3(
			RandomRange(m_parameters.minVelocity.x, m_parameters.maxVelocity.x),
			RandomRange(m_parameters.minVelocity.y, m_parameters.maxVelocity.y),
			RandomRange(m_parameters.minVelocity.z, m_parameters.maxVelocity.z)
		);
	}

	float ParticleEmitter::RandomRange(float min, float max) const
	{
		std::uniform_real_distribution<float> dist(min, max);
		return dist(RandomGenerator);
	}
}
