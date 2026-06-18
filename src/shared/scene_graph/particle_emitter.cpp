// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "particle_emitter.h"

#include "camera.h"
#include "material_manager.h"
#include "mesh.h"
#include "mesh_manager.h"
#include "sub_mesh.h"
#include "movable_object.h"
#include "node.h"
#include "render_operation.h"
#include "render_queue.h"
#include "scene.h"
#include "graphics/graphics_device.h"
#include "graphics/material.h"
#include "graphics/vertex_format.h"
#include "graphics/vertex_declaration.h"
#include "math/matrix4.h"
#include "math/quaternion.h"
#include "math/radian.h"
#include "base/random.h"
#include "log/default_log_levels.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

namespace mmo
{
	const String ParticleSystem::TYPE_NAME = "ParticleSystem";

	namespace
	{
		float RandomRange(float minValue, float maxValue)
		{
			if (maxValue <= minValue)
			{
				return minValue;
			}

			std::uniform_real_distribution<float> dist(minValue, maxValue);
			return dist(RandomGenerator);
		}

		Vector3 RandomRange(const Vector3& minValue, const Vector3& maxValue)
		{
			return Vector3(
				RandomRange(minValue.x, maxValue.x),
				RandomRange(minValue.y, maxValue.y),
				RandomRange(minValue.z, maxValue.z));
		}

		/// @brief Cheap, smooth, roughly divergence-free turbulence field. No texture lookups.
		Vector3 CurlNoise(const Vector3& p, float t)
		{
			return Vector3(
				std::sin(p.y * 1.7f + t * 0.9f) + std::cos(p.z * 1.3f - t * 1.1f),
				std::sin(p.z * 1.5f + t * 1.2f) + std::cos(p.x * 1.1f - t * 0.7f),
				std::sin(p.x * 1.9f + t * 0.8f) + std::cos(p.y * 1.4f - t * 1.3f)) * 0.5f;
		}

		uint32 PackColorAbgr(const Vector4& color)
		{
			const auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
			return
				(static_cast<uint32>(clamp01(color.w) * 255.0f) << 24) |
				(static_cast<uint32>(clamp01(color.z) * 255.0f) << 16) |
				(static_cast<uint32>(clamp01(color.y) * 255.0f) << 8) |
				(static_cast<uint32>(clamp01(color.x) * 255.0f));
		}
	}

	// ========================================================================
	// ParticleRenderable
	// ========================================================================

	ParticleRenderable::ParticleRenderable(GraphicsDevice& device, EmitterInstance& parent)
		: m_device(device)
		, m_parent(parent)
	{
		m_vertexData = std::make_unique<VertexData>(&m_device);

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

		m_indexData = std::make_unique<IndexData>();
	}

	void ParticleRenderable::PrepareRenderOperation(RenderOperation& operation)
	{
		operation.topology = TopologyType::TriangleList;
		operation.vertexFormat = VertexFormat::PosColorNormalBinormalTangentTex1;
		operation.vertexData = m_vertexData.get();
		operation.indexData = m_indexData.get();
	}

	const Matrix4& ParticleRenderable::GetWorldTransform() const
	{
		// Particle positions are baked into world space inside RebuildBuffers, so the world
		// transform is always identity.
		return Matrix4::Identity;
	}

	float ParticleRenderable::GetSquaredViewDepth(const Camera& camera) const
	{
		const Vector3 emitterPos = m_parent.GetEmitterWorldPosition();
		const Vector3 diff = emitterPos - camera.GetDerivedPosition();
		return diff.GetSquaredLength();
	}

	MaterialPtr ParticleRenderable::GetMaterial() const
	{
		return m_parent.GetMaterial();
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

		const size_t vertexCount = particleCount * 4;
		const size_t indexCount = particleCount * 6;

		const EmitterParameters& params = m_parent.GetParameters();
		const Matrix4& bake = m_parent.GetWorldTransform();

		const Quaternion& camOrientation = camera.GetDerivedOrientation();
		const Vector3 camRight = camOrientation * Vector3::UnitX;
		const Vector3 camUp = camOrientation * Vector3::UnitY;
		const Vector3 camForward = camOrientation * (-Vector3::UnitZ);

		const uint32 spriteCols = std::max<uint32>(1, params.spriteSheetColumns);
		const uint32 spriteRows = std::max<uint32>(1, params.spriteSheetRows);
		const uint32 spriteTotal = spriteCols * spriteRows;
		const float du = 1.0f / static_cast<float>(spriteCols);
		const float dv = 1.0f / static_cast<float>(spriteRows);

		EnsureIndexBuffer(particleCount);

		const size_t requiredBufferSize = vertexCount;
		if (!m_vertexBuffer || m_vertexBuffer->GetVertexCount() < requiredBufferSize)
		{
			size_t newSize = requiredBufferSize;
			if (m_vertexBuffer)
			{
				newSize = m_vertexBuffer->GetVertexCount() * 2;
				if (newSize < requiredBufferSize)
				{
					newSize = requiredBufferSize;
				}
			}

			m_vertexBuffer = m_device.CreateVertexBuffer(
				newSize,
				sizeof(POS_COL_NORMAL_BINORMAL_TANGENT_TEX_VERTEX),
				BufferUsage::DynamicWriteOnlyDiscardable,
				nullptr);

			m_vertexData->vertexBufferBinding->SetBinding(0, m_vertexBuffer);
		}

		auto* vertices = static_cast<POS_COL_NORMAL_BINORMAL_TANGENT_TEX_VERTEX*>(m_vertexBuffer->Map(LockOptions::Discard));

		const Vector3 normal = camForward;

		for (size_t i = 0; i < particleCount; ++i)
		{
			const Particle& particle = particles[i];

			const Vector3 worldPos = bake.TransformAffine(particle.position);
			const Vector3 worldVel = bake.TransformDirectionAffine(particle.velocity);
			const float halfSize = particle.size * 0.5f;

			Vector3 rightOffset;
			Vector3 upOffset;

			switch (params.renderMode)
			{
			case ParticleRenderMode::VelocityAligned:
			case ParticleRenderMode::Stretched:
			{
				Vector3 upDir = worldVel;
				const float speed = upDir.GetLength();
				if (speed > 1e-4f)
				{
					upDir /= speed;
				}
				else
				{
					upDir = camUp;
				}

				Vector3 rightDir = upDir.Cross(camForward);
				if (rightDir.GetSquaredLength() < 1e-6f)
				{
					rightDir = camRight;
				}
				else
				{
					rightDir.Normalize();
				}

				const float lengthHalf = (params.renderMode == ParticleRenderMode::Stretched)
					? halfSize * std::max(1.0f, params.lengthScale)
					: halfSize;

				rightOffset = rightDir * halfSize;
				upOffset = upDir * lengthHalf;
				break;
			}

			case ParticleRenderMode::HorizontalBillboard:
			{
				Vector3 right = Vector3::UnitX;
				Vector3 up = Vector3::UnitZ;
				if (particle.rotation != 0.0f)
				{
					const float c = std::cos(particle.rotation);
					const float s = std::sin(particle.rotation);
					const Vector3 r2 = right * c + up * s;
					const Vector3 u2 = right * (-s) + up * c;
					right = r2;
					up = u2;
				}
				rightOffset = right * halfSize;
				upOffset = up * halfSize;
				break;
			}

			case ParticleRenderMode::BillboardFacing:
			default:
			{
				Vector3 right = camRight;
				Vector3 up = camUp;
				if (particle.rotation != 0.0f)
				{
					const float c = std::cos(particle.rotation);
					const float s = std::sin(particle.rotation);
					const Vector3 r2 = right * c + up * s;
					const Vector3 u2 = right * (-s) + up * c;
					right = r2;
					up = u2;
				}
				rightOffset = right * halfSize;
				upOffset = up * halfSize;
				break;
			}
			}

			const uint32 color = PackColorAbgr(particle.color);

			const uint32 frame = (spriteTotal > 0) ? (particle.spriteIndex % spriteTotal) : 0;
			const uint32 col = frame % spriteCols;
			const uint32 row = frame / spriteCols;
			const float uMin = col * du;
			const float uMax = uMin + du;
			const float vMin = row * dv;
			const float vMax = vMin + dv;

			const Vector3 tangent = rightOffset.NormalizedCopy();
			const Vector3 binormal = upOffset.NormalizedCopy();

			const size_t baseIndex = i * 4;

			// Bottom-left
			vertices[baseIndex + 0].pos = worldPos - rightOffset - upOffset;
			vertices[baseIndex + 0].color = color;
			vertices[baseIndex + 0].normal = normal;
			vertices[baseIndex + 0].binormal = binormal;
			vertices[baseIndex + 0].tangent = tangent;
			vertices[baseIndex + 0].uv[0] = uMin;
			vertices[baseIndex + 0].uv[1] = vMax;

			// Bottom-right
			vertices[baseIndex + 1].pos = worldPos + rightOffset - upOffset;
			vertices[baseIndex + 1].color = color;
			vertices[baseIndex + 1].normal = normal;
			vertices[baseIndex + 1].binormal = binormal;
			vertices[baseIndex + 1].tangent = tangent;
			vertices[baseIndex + 1].uv[0] = uMax;
			vertices[baseIndex + 1].uv[1] = vMax;

			// Top-right
			vertices[baseIndex + 2].pos = worldPos + rightOffset + upOffset;
			vertices[baseIndex + 2].color = color;
			vertices[baseIndex + 2].normal = normal;
			vertices[baseIndex + 2].binormal = binormal;
			vertices[baseIndex + 2].tangent = tangent;
			vertices[baseIndex + 2].uv[0] = uMax;
			vertices[baseIndex + 2].uv[1] = vMin;

			// Top-left
			vertices[baseIndex + 3].pos = worldPos - rightOffset + upOffset;
			vertices[baseIndex + 3].color = color;
			vertices[baseIndex + 3].normal = normal;
			vertices[baseIndex + 3].binormal = binormal;
			vertices[baseIndex + 3].tangent = tangent;
			vertices[baseIndex + 3].uv[0] = uMin;
			vertices[baseIndex + 3].uv[1] = vMin;
		}

		m_vertexBuffer->Unmap();

		m_vertexData->vertexCount = static_cast<uint32>(vertexCount);
		m_vertexData->vertexStart = 0;
		m_indexData->indexCount = indexCount;
		m_indexData->indexStart = 0;
	}

	void ParticleRenderable::SortParticles(std::vector<Particle>& particles, const Camera& camera) const
	{
		if (particles.empty())
		{
			return;
		}

		const Vector3 cameraPos = camera.GetDerivedPosition();
		const Matrix4& bake = m_parent.GetWorldTransform();

		std::sort(particles.begin(), particles.end(),
			[&cameraPos, &bake](const Particle& a, const Particle& b) -> bool
			{
				const float distA = (bake.TransformAffine(a.position) - cameraPos).GetSquaredLength();
				const float distB = (bake.TransformAffine(b.position) - cameraPos).GetSquaredLength();
				return distA > distB;
			});
	}

	void ParticleRenderable::EnsureIndexBuffer(size_t particleCount)
	{
		const size_t requiredIndexCount = particleCount * 6;

		if (!m_indexBuffer || m_indexBufferCapacity < requiredIndexCount)
		{
			size_t newCapacity = requiredIndexCount;
			if (m_indexBuffer)
			{
				newCapacity = m_indexBufferCapacity * 2;
				if (newCapacity < requiredIndexCount)
				{
					newCapacity = requiredIndexCount;
				}
			}

			m_indexBuffer = m_device.CreateIndexBuffer(
				newCapacity,
				IndexBufferSize::Index_16,
				BufferUsage::StaticWriteOnly);

			auto* indices = static_cast<uint16*>(m_indexBuffer->Map(LockOptions::Normal));

			for (size_t i = 0; i < newCapacity / 6; ++i)
			{
				const uint16 baseVertex = static_cast<uint16>(i * 4);
				const size_t baseIndex = i * 6;

				indices[baseIndex + 0] = baseVertex + 0;
				indices[baseIndex + 1] = baseVertex + 1;
				indices[baseIndex + 2] = baseVertex + 2;
				indices[baseIndex + 3] = baseVertex + 2;
				indices[baseIndex + 4] = baseVertex + 3;
				indices[baseIndex + 5] = baseVertex + 0;
			}

			m_indexBuffer->Unmap();

			m_indexBufferCapacity = newCapacity;
			m_indexData->indexBuffer = m_indexBuffer;
		}
	}

	// ========================================================================
	// ParticleMeshRenderable
	// ========================================================================

	ParticleMeshRenderable::ParticleMeshRenderable(EmitterInstance& parent, VertexData* vertexData,
		IndexData* indexData, MaterialPtr material)
		: m_parent(parent)
		, m_vertexData(vertexData)
		, m_indexData(indexData)
		, m_material(std::move(material))
	{
	}

	void ParticleMeshRenderable::PrepareRenderOperation(RenderOperation& operation)
	{
		operation.topology = TopologyType::TriangleList;
		operation.vertexData = m_vertexData;
		operation.indexData = m_indexData;
		operation.material = m_material;

		// Per-particle transform + tint are uploaded as instance data; the device selects the
		// material's instanced vertex-shader variant when an instance buffer is present.
		operation.instanceBuffer = m_parent.GetInstanceBuffer();
		operation.instanceCount = m_parent.GetInstanceCount();
	}

	const Matrix4& ParticleMeshRenderable::GetWorldTransform() const
	{
		// Per-particle transforms live in the instance buffer; the base transform is identity.
		return Matrix4::Identity;
	}

	float ParticleMeshRenderable::GetSquaredViewDepth(const Camera& camera) const
	{
		const Vector3 diff = m_parent.GetEmitterWorldPosition() - camera.GetDerivedPosition();
		return diff.GetSquaredLength();
	}

	MaterialPtr ParticleMeshRenderable::GetMaterial() const
	{
		return m_material;
	}

	bool ParticleMeshRenderable::IsReady() const
	{
		return m_vertexData != nullptr && m_indexData != nullptr
			&& m_parent.GetInstanceBuffer() != nullptr && m_parent.GetInstanceCount() > 0;
	}

	// ========================================================================
	// EmitterInstance
	// ========================================================================

	EmitterInstance::EmitterInstance(GraphicsDevice& device, ParticleSystem& system)
		: m_device(device)
		, m_system(system)
	{
		m_renderable = std::make_unique<ParticleRenderable>(m_device, *this);
		m_boundingBox.min = Vector3::Zero;
		m_boundingBox.max = Vector3::Zero;
		m_particles.reserve(m_parameters.maxParticles);
		m_burstFired.resize(m_parameters.bursts.size(), 0);
	}

	void EmitterInstance::SetParameters(const EmitterParameters& params)
	{
		m_parameters = params;
		m_particles.reserve(params.maxParticles);
		if (m_particles.size() > params.maxParticles)
		{
			m_particles.resize(params.maxParticles);
		}

		m_burstFired.assign(params.bursts.size(), 0);

		if (!params.materialName.empty())
		{
			try
			{
				m_material = MaterialManager::Get().Load(params.materialName);
			}
			catch (const std::exception& e)
			{
				WLOG("EmitterInstance: failed to load material '" << params.materialName << "': " << e.what());
			}
		}
		else
		{
			m_material = nullptr;
		}

		// Mesh render mode: clone the source mesh's geometry for GPU instancing.
		m_meshMode = (m_parameters.renderMode == ParticleRenderMode::Mesh);
		if (m_meshMode)
		{
			if (m_loadedMeshName != m_parameters.meshName || m_meshRenderables.empty())
			{
				RebuildMeshResources();
			}
		}
		else if (!m_loadedMeshName.empty() || !m_meshRenderables.empty())
		{
			// Switched away from mesh mode: release the cloned geometry and instance buffer.
			m_meshRenderables.clear();
			m_meshVertexData.clear();
			m_meshIndexData.clear();
			m_mesh.reset();
			m_loadedMeshName.clear();
			m_instanceBuffer.reset();
			m_instanceBufferCapacity = 0;
			m_instanceCount = 0;
		}
	}

	void EmitterInstance::RebuildMeshResources()
	{
		// Release renderables first so their raw pointers into the geometry vectors never dangle.
		m_meshRenderables.clear();
		m_meshVertexData.clear();
		m_meshIndexData.clear();
		m_mesh.reset();
		m_loadedMeshName = m_parameters.meshName;

		if (m_parameters.meshName.empty())
		{
			return;
		}

		try
		{
			m_mesh = MeshManager::Get().Load(m_parameters.meshName);
		}
		catch (const std::exception& e)
		{
			WLOG("EmitterInstance: failed to load mesh '" << m_parameters.meshName << "': " << e.what());
			m_mesh.reset();
		}

		if (!m_mesh)
		{
			return;
		}

		const uint16 submeshCount = m_mesh->GetSubMeshCount();
		for (uint16 i = 0; i < submeshCount; ++i)
		{
			SubMesh& subMesh = m_mesh->GetSubMesh(i);

			std::unique_ptr<VertexData> vertexData;
			if (subMesh.useSharedVertices && m_mesh->sharedVertexData)
			{
				vertexData.reset(m_mesh->sharedVertexData->Clone(false, &m_device));
			}
			else if (subMesh.vertexData)
			{
				vertexData.reset(subMesh.vertexData->Clone(false, &m_device));
			}

			std::unique_ptr<IndexData> indexData;
			if (subMesh.indexData)
			{
				indexData.reset(subMesh.indexData->Clone(false));
			}

			if (!vertexData || !indexData)
			{
				continue;
			}

			auto renderable = std::make_unique<ParticleMeshRenderable>(
				*this, vertexData.get(), indexData.get(), subMesh.GetMaterial());

			m_meshVertexData.push_back(std::move(vertexData));
			m_meshIndexData.push_back(std::move(indexData));
			m_meshRenderables.push_back(std::move(renderable));
		}
	}

	void EmitterInstance::RebuildInstanceBuffer(const Camera& camera)
	{
		m_instanceCount = 0;

		const size_t particleCount = m_particles.size();
		if (particleCount == 0)
		{
			return;
		}

		// Back-to-front sort for translucent mesh materials so per-particle alpha blends correctly.
		const MaterialPtr firstMaterial = m_meshRenderables.empty() ? nullptr : m_meshRenderables.front()->GetMaterial();
		if (firstMaterial && firstMaterial->IsTranslucent())
		{
			m_renderable->SortParticles(m_particles, camera);
		}

		if (!m_instanceBuffer || m_instanceBufferCapacity < particleCount)
		{
			size_t newCapacity = particleCount;
			if (m_instanceBuffer && m_instanceBufferCapacity * 2 > newCapacity)
			{
				newCapacity = m_instanceBufferCapacity * 2;
			}

			m_instanceBuffer = m_device.CreateVertexBuffer(
				newCapacity,
				sizeof(ParticleMeshInstance),
				BufferUsage::DynamicWriteOnlyDiscardable,
				nullptr);
			m_instanceBufferCapacity = newCapacity;
		}

		const Matrix4& bake = m_renderTransform;

		auto* instances = static_cast<ParticleMeshInstance*>(m_instanceBuffer->Map(LockOptions::Discard));
		for (size_t i = 0; i < particleCount; ++i)
		{
			const Particle& particle = m_particles[i];
			const Vector3 worldPos = bake.TransformAffine(particle.position);
			const Quaternion rotation(Radian(particle.rotation), Vector3::UnitY);
			const float scale = (particle.size > 1e-4f) ? particle.size : 1e-4f;

			instances[i].world.MakeTransform(worldPos, Vector3(scale, scale, scale), rotation);
			instances[i].color = particle.color;
		}
		m_instanceBuffer->Unmap();

		m_instanceCount = static_cast<uint32>(particleCount);
	}

	void EmitterInstance::Reset()
	{
		m_particles.clear();
		m_spawnAccumulator = 0.0f;
		m_age = 0.0f;
		m_cycleTime = 0.0f;
		m_noiseTime = 0.0f;
		m_warmedUp = false;
		std::fill(m_burstFired.begin(), m_burstFired.end(), static_cast<uint8>(0));
	}

	bool EmitterInstance::IsFinished() const
	{
		if (m_parameters.loop)
		{
			return false;
		}

		return (m_age >= (m_parameters.startDelay + m_parameters.duration)) && m_particles.empty();
	}

	Vector3 EmitterInstance::GetSpawnPosition(Vector3& outDirection) const
	{
		switch (m_parameters.shape)
		{
		case EmitterShape::Sphere:
		{
			const float radius = m_parameters.shapeExtents.x;
			Vector3 offset;
			do
			{
				offset = Vector3(RandomRange(-1.0f, 1.0f), RandomRange(-1.0f, 1.0f), RandomRange(-1.0f, 1.0f));
			} while (offset.GetSquaredLength() > 1.0f);

			outDirection = offset.GetSquaredLength() > 1e-6f ? offset.NormalizedCopy() : Vector3::UnitY;
			return offset * radius;
		}

		case EmitterShape::Box:
		{
			const Vector3 halfExtents = m_parameters.shapeExtents * 0.5f;
			outDirection = Vector3::UnitY;
			return Vector3(
				RandomRange(-halfExtents.x, halfExtents.x),
				RandomRange(-halfExtents.y, halfExtents.y),
				RandomRange(-halfExtents.z, halfExtents.z));
		}

		case EmitterShape::Cone:
		{
			const float height = m_parameters.shapeExtents.y;
			const float baseRadius = m_parameters.shapeExtents.z;

			const float t = RandomRange(0.0f, 1.0f);
			const float currentHeight = height * t;
			const float currentRadius = baseRadius * t;
			const float theta = RandomRange(0.0f, 2.0f * 3.14159265f);

			const Vector3 offset(currentRadius * std::cos(theta), currentHeight, currentRadius * std::sin(theta));
			outDirection = offset.GetSquaredLength() > 1e-6f ? offset.NormalizedCopy() : Vector3::UnitY;
			return offset;
		}

		case EmitterShape::Point:
		default:
			outDirection = Vector3::UnitY;
			return Vector3::Zero;
		}
	}

	void EmitterInstance::SpawnOne(const Matrix4& systemWorld, const Vector3& systemWorldPos, const Vector3& systemVelocity)
	{
		if (m_particles.size() >= m_parameters.maxParticles)
		{
			return;
		}

		Particle particle;

		Vector3 spawnDir;
		Vector3 localPos = GetSpawnPosition(spawnDir);

		const float startSpeed = RandomRange(m_parameters.minStartSpeed, m_parameters.maxStartSpeed);
		Vector3 localVel = RandomRange(m_parameters.minVelocity, m_parameters.maxVelocity) + spawnDir * startSpeed;

		if (m_parameters.simulationSpace == SimulationSpace::World)
		{
			particle.position = systemWorld.TransformAffine(localPos);
			particle.velocity = systemWorld.TransformDirectionAffine(localVel) + systemVelocity * m_parameters.inheritVelocity;
		}
		else
		{
			// Local space: keep particle coordinates relative to the emitter; the renderable bakes
			// them to world space each frame using the current node transform.
			particle.position = localPos;
			particle.velocity = localVel;
		}

		particle.lifetime = RandomRange(m_parameters.minLifetime, m_parameters.maxLifetime);
		if (particle.lifetime <= 0.0f)
		{
			particle.lifetime = 0.0001f;
		}
		particle.age = 0.0f;
		particle.baseSize = RandomRange(m_parameters.minStartSize, m_parameters.maxStartSize);
		particle.size = particle.baseSize * m_parameters.sizeOverLife.Evaluate(0.0f);
		particle.rotation = RandomRange(m_parameters.minStartRotation, m_parameters.maxStartRotation);
		particle.angularVelocity = RandomRange(m_parameters.minAngularVelocity, m_parameters.maxAngularVelocity);
		particle.color = m_parameters.colorOverLifetime.Evaluate(0.0f);
		particle.randomPhase = RandomRange(0.0f, 1.0f);
		particle.pad0 = 0.0f;
		particle.pad1 = 0.0f;

		const uint32 spriteTotal = std::max<uint32>(1, m_parameters.spriteSheetColumns) * std::max<uint32>(1, m_parameters.spriteSheetRows);
		if (m_parameters.spriteAnimation == SpriteAnimationMode::RandomStatic && spriteTotal > 1)
		{
			particle.spriteIndex = static_cast<uint32>(RandomRange(0.0f, static_cast<float>(spriteTotal))) % spriteTotal;
		}
		else
		{
			particle.spriteIndex = 0;
		}

		m_particles.push_back(particle);
	}

	void EmitterInstance::UpdateParticles(float deltaTime)
	{
		const bool localSpace = (m_parameters.simulationSpace == SimulationSpace::Local);
		const Vector3 forceCenter = localSpace ? Vector3::Zero : m_emitterWorldPos;
		const Vector3 attractorWorld = localSpace ? m_parameters.attractorPosition : (m_emitterWorldPos + m_parameters.attractorPosition);

		const bool useOrbital = std::abs(m_parameters.orbitalSpeed) > 1e-6f;
		const bool useRadial = std::abs(m_parameters.radialAcceleration) > 1e-6f;
		const bool useAttractor = std::abs(m_parameters.attractorStrength) > 1e-6f;
		const bool useNoise = m_parameters.noiseAmplitude > 1e-6f;
		const bool useDrag = m_parameters.drag > 1e-6f;

		const uint32 spriteTotal = std::max<uint32>(1, m_parameters.spriteSheetColumns) * std::max<uint32>(1, m_parameters.spriteSheetRows);

		const float orbitAngle = m_parameters.orbitalSpeed * deltaTime;
		const float cosA = std::cos(orbitAngle);
		const float sinA = std::sin(orbitAngle);

		for (auto& particle : m_particles)
		{
			particle.age += deltaTime;
			if (particle.age >= particle.lifetime)
			{
				continue;
			}

			// Gravity
			particle.velocity += m_parameters.gravity * deltaTime;

			// Radial acceleration (outward / inward from the emitter center)
			if (useRadial)
			{
				Vector3 radial = particle.position - forceCenter;
				if (radial.GetSquaredLength() > 1e-6f)
				{
					radial.Normalize();
					particle.velocity += radial * (m_parameters.radialAcceleration * deltaTime);
				}
			}

			// Point attractor
			if (useAttractor)
			{
				Vector3 toAttractor = attractorWorld - particle.position;
				if (toAttractor.GetSquaredLength() > 1e-6f)
				{
					toAttractor.Normalize();
					particle.velocity += toAttractor * (m_parameters.attractorStrength * deltaTime);
				}
			}

			// Turbulence / curl noise
			if (useNoise)
			{
				const Vector3 samplePos = particle.position * m_parameters.noiseFrequency + Vector3(particle.randomPhase * 13.0f, 0.0f, 0.0f);
				particle.velocity += CurlNoise(samplePos, m_noiseTime) * (m_parameters.noiseAmplitude * deltaTime);
			}

			// Linear drag
			if (useDrag)
			{
				const float damp = 1.0f - m_parameters.drag * deltaTime;
				particle.velocity *= (damp < 0.0f) ? 0.0f : damp;
			}

			// Integrate position
			particle.position += particle.velocity * deltaTime;

			// Orbital swirl around the emitter's Y axis (position-based)
			if (useOrbital)
			{
				const Vector3 offset = particle.position - forceCenter;
				const float rx = offset.x * cosA - offset.z * sinA;
				const float rz = offset.x * sinA + offset.z * cosA;
				particle.position = forceCenter + Vector3(rx, offset.y, rz);
			}

			// Rotation
			particle.rotation += particle.angularVelocity * deltaTime;

			const float t = particle.age / particle.lifetime;

			// Size & colour over life
			particle.size = particle.baseSize * m_parameters.sizeOverLife.Evaluate(t);
			particle.color = m_parameters.colorOverLifetime.Evaluate(t);

			// Sprite-sheet animation
			if (m_parameters.spriteAnimation == SpriteAnimationMode::AnimateOverLife && spriteTotal > 1)
			{
				uint32 frame;
				if (m_parameters.spriteAnimationFps > 0.0f)
				{
					frame = static_cast<uint32>(particle.age * m_parameters.spriteAnimationFps) % spriteTotal;
				}
				else
				{
					frame = static_cast<uint32>(t * static_cast<float>(spriteTotal));
					if (frame >= spriteTotal)
					{
						frame = spriteTotal - 1;
					}
				}
				particle.spriteIndex = frame;
			}
		}

		m_particles.erase(
			std::remove_if(m_particles.begin(), m_particles.end(),
				[](const Particle& p) { return p.age >= p.lifetime; }),
			m_particles.end());
	}

	void EmitterInstance::UpdateBoundingBox()
	{
		if (m_particles.empty())
		{
			m_boundingBox.min = m_emitterWorldPos;
			m_boundingBox.max = m_emitterWorldPos;
			return;
		}

		const Matrix4& bake = m_renderTransform;
		Vector3 minPos = bake.TransformAffine(m_particles[0].position);
		Vector3 maxPos = minPos;

		for (const auto& particle : m_particles)
		{
			const Vector3 world = bake.TransformAffine(particle.position);
			const Vector3 halfSize(particle.size * 0.5f, particle.size * 0.5f, particle.size * 0.5f);
			const Vector3 pMin = world - halfSize;
			const Vector3 pMax = world + halfSize;

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

	void EmitterInstance::SimulateWarmup(const Matrix4& systemWorld, const Vector3& systemWorldPos, const Vector3& systemVelocity)
	{
		m_warmedUp = true;

		if (m_parameters.warmupTime <= 0.0f)
		{
			return;
		}

		const float step = 1.0f / 30.0f;
		float remaining = m_parameters.warmupTime;
		// Cap the number of warm-up steps to avoid pathological stalls.
		int safety = 600;
		while (remaining > 0.0f && safety-- > 0)
		{
			const float dt = std::min(step, remaining);
			remaining -= dt;
			Update(dt, systemWorld, systemWorldPos, systemVelocity, nullptr);
		}
	}

	void EmitterInstance::Update(float deltaTime, const Matrix4& systemWorld, const Vector3& systemWorldPos,
		const Vector3& systemVelocity, const Camera* camera)
	{
		if (!m_parameters.enabled)
		{
			m_particles.clear();
			return;
		}

		// Cache transforms used by the simulation and renderable.
		m_renderTransform = (m_parameters.simulationSpace == SimulationSpace::Local) ? systemWorld : Matrix4::Identity;
		m_emitterWorldPos = systemWorldPos;

		if (!m_warmedUp && m_parameters.warmupTime > 0.0f)
		{
			SimulateWarmup(systemWorld, systemWorldPos, systemVelocity);
		}
		m_warmedUp = true;

		m_age += deltaTime;
		m_cycleTime += deltaTime;
		m_noiseTime += deltaTime;

		// Looping handling: wrap the cycle and re-arm bursts.
		if (m_parameters.loop && m_parameters.duration > 0.0f)
		{
			while (m_cycleTime >= m_parameters.duration)
			{
				m_cycleTime -= m_parameters.duration;
				std::fill(m_burstFired.begin(), m_burstFired.end(), static_cast<uint8>(0));
			}
		}

		const bool pastDelay = m_age >= m_parameters.startDelay;
		const float cycleTimeForBurst = m_cycleTime - (m_parameters.loop ? 0.0f : m_parameters.startDelay);

		// Determine whether the emitter is still actively emitting.
		bool emittingNow = m_emitting && pastDelay;
		if (!m_parameters.loop && m_age >= (m_parameters.startDelay + m_parameters.duration))
		{
			emittingNow = false;
		}

		if (emittingNow)
		{
			// Continuous emission
			if (m_parameters.spawnRate > 0.0f)
			{
				m_spawnAccumulator += deltaTime * m_parameters.spawnRate;
				while (m_spawnAccumulator >= 1.0f && m_particles.size() < m_parameters.maxParticles)
				{
					SpawnOne(systemWorld, systemWorldPos, systemVelocity);
					m_spawnAccumulator -= 1.0f;
				}
			}

			// Bursts
			if (m_burstFired.size() != m_parameters.bursts.size())
			{
				m_burstFired.assign(m_parameters.bursts.size(), 0);
			}
			for (size_t i = 0; i < m_parameters.bursts.size(); ++i)
			{
				if (!m_burstFired[i] && cycleTimeForBurst >= m_parameters.bursts[i].time)
				{
					const uint32 count = m_parameters.bursts[i].count;
					for (uint32 c = 0; c < count && m_particles.size() < m_parameters.maxParticles; ++c)
					{
						SpawnOne(systemWorld, systemWorldPos, systemVelocity);
					}
					m_burstFired[i] = 1;
				}
			}
		}

		UpdateParticles(deltaTime);
		UpdateBoundingBox();

		// Only (re)build GPU buffers when a camera is available (skipped during head-less warm-up).
		// RebuildBuffers safely clears the buffers when the particle list is empty.
		if (camera)
		{
			if (IsMeshMode())
			{
				RebuildInstanceBuffer(*camera);
			}
			else
			{
				if (!m_particles.empty() && m_material && m_material->IsTranslucent())
				{
					m_renderable->SortParticles(m_particles, *camera);
				}
				m_renderable->RebuildBuffers(m_particles, *camera);
			}
		}
	}

	// ========================================================================
	// ParticleSystem
	// ========================================================================

	ParticleSystem::ParticleSystem(const String& name, GraphicsDevice& device)
		: m_device(device)
	{
		m_name = name;
		m_boundingBox.min = Vector3::Zero;
		m_boundingBox.max = Vector3::Zero;
		m_lastUpdateTime = std::chrono::high_resolution_clock::now();

		// Start with one default emitter so the legacy single-emitter API works out of the box.
		AddEmitter(EmitterParameters());

		SetRenderQueueGroup(RenderQueueGroupId::Transparent);
	}

	const String& ParticleSystem::GetMovableType() const
	{
		return TYPE_NAME;
	}

	const AABB& ParticleSystem::GetBoundingBox() const
	{
		return m_boundingBox;
	}

	float ParticleSystem::GetBoundingRadius() const
	{
		return m_boundingBox.GetExtents().GetLength();
	}

	EmitterInstance* ParticleSystem::GetEmitter(size_t index) const
	{
		if (index >= m_emitters.size())
		{
			return nullptr;
		}
		return m_emitters[index].get();
	}

	EmitterInstance* ParticleSystem::AddEmitter(const EmitterParameters& params)
	{
		auto emitter = std::make_unique<EmitterInstance>(m_device, *this);
		emitter->SetParameters(params);
		if (m_isPlaying)
		{
			emitter->Play();
		}
		else
		{
			emitter->Stop();
		}
		m_emitters.push_back(std::move(emitter));
		return m_emitters.back().get();
	}

	void ParticleSystem::RemoveEmitter(size_t index)
	{
		if (index < m_emitters.size())
		{
			m_emitters.erase(m_emitters.begin() + index);
		}
	}

	void ParticleSystem::ClearEmitters()
	{
		m_emitters.clear();
	}

	void ParticleSystem::SetSystemParameters(const ParticleSystemParameters& params)
	{
		m_emitters.clear();
		if (params.emitters.empty())
		{
			AddEmitter(EmitterParameters());
		}
		else
		{
			for (const auto& emitterParams : params.emitters)
			{
				AddEmitter(emitterParams);
			}
		}
	}

	ParticleSystemParameters ParticleSystem::GetSystemParameters() const
	{
		ParticleSystemParameters params;
		params.emitters.reserve(m_emitters.size());
		for (const auto& emitter : m_emitters)
		{
			params.emitters.push_back(emitter->GetParameters());
		}
		return params;
	}

	void ParticleSystem::SetParameters(const EmitterParameters& params)
	{
		if (m_emitters.empty())
		{
			AddEmitter(params);
		}
		else
		{
			m_emitters[0]->SetParameters(params);
		}
	}

	const EmitterParameters& ParticleSystem::GetParameters() const
	{
		static const EmitterParameters s_default;
		if (m_emitters.empty())
		{
			return s_default;
		}
		return m_emitters[0]->GetParameters();
	}

	void ParticleSystem::SetMaterial(const MaterialPtr& material)
	{
		if (!m_emitters.empty())
		{
			m_emitters[0]->SetMaterial(material);
		}
	}

	MaterialPtr ParticleSystem::GetMaterial() const
	{
		if (m_emitters.empty())
		{
			return nullptr;
		}
		return m_emitters[0]->GetMaterial();
	}

	void ParticleSystem::Play()
	{
		m_isPlaying = true;
		for (auto& emitter : m_emitters)
		{
			emitter->Play();
		}
	}

	void ParticleSystem::Stop()
	{
		m_isPlaying = false;
		for (auto& emitter : m_emitters)
		{
			emitter->Stop();
		}
	}

	void ParticleSystem::Reset()
	{
		for (auto& emitter : m_emitters)
		{
			emitter->Reset();
		}
		m_lastUpdateTime = std::chrono::high_resolution_clock::now();
		m_hasLastWorldPos = false;
	}

	bool ParticleSystem::IsFinished() const
	{
		for (const auto& emitter : m_emitters)
		{
			if (!emitter->IsFinished())
			{
				return false;
			}
		}
		return !m_emitters.empty();
	}

	Vector3 ParticleSystem::GetDerivedPosition() const
	{
		if (m_parentNode)
		{
			return m_parentNode->GetDerivedPosition();
		}
		return Vector3::Zero;
	}

	size_t ParticleSystem::GetTotalParticleCount() const
	{
		size_t total = 0;
		for (const auto& emitter : m_emitters)
		{
			total += emitter->GetParticleCount();
		}
		return total;
	}

	void ParticleSystem::Update(float deltaTime)
	{
		// Clamp to avoid huge jumps (debugger pauses, hitches).
		const float dt = std::min(std::max(deltaTime, 0.0f), 0.1f);

		Matrix4 worldMatrix = Matrix4::Identity;
		Vector3 worldPos = Vector3::Zero;
		if (m_parentNode)
		{
			worldMatrix = m_parentNode->GetFullTransform();
			worldPos = m_parentNode->GetDerivedPosition();
		}

		Vector3 systemVelocity = Vector3::Zero;
		if (m_hasLastWorldPos && dt > 1e-5f)
		{
			systemVelocity = (worldPos - m_lastWorldPos) / dt;
		}
		m_lastWorldPos = worldPos;
		m_hasLastWorldPos = true;

		Camera* camera = m_scene ? m_scene->GetCamera(0) : nullptr;

		for (auto& emitter : m_emitters)
		{
			emitter->Update(dt, worldMatrix, worldPos, systemVelocity, camera);
		}

		UpdateBoundingBox();
	}

	void ParticleSystem::Update()
	{
		const auto currentTime = std::chrono::high_resolution_clock::now();
		const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - m_lastUpdateTime);
		const float deltaTime = duration.count() / 1000000.0f;
		m_lastUpdateTime = currentTime;

		Update(deltaTime);
	}

	void ParticleSystem::PopulateRenderQueue(RenderQueue& queue)
	{
		for (auto& emitter : m_emitters)
		{
			if (!emitter->GetParameters().enabled)
			{
				continue;
			}

			if (emitter->IsMeshMode())
			{
				for (const auto& meshRenderable : emitter->GetMeshRenderables())
				{
					if (meshRenderable && meshRenderable->GetMaterial() && meshRenderable->IsReady())
					{
						queue.AddRenderable(*meshRenderable, GetRenderQueueGroup(), 0);
					}
				}
				continue;
			}

			ParticleRenderable* renderable = emitter->GetRenderable();
			if (renderable && emitter->GetMaterial() && renderable->IsReady())
			{
				queue.AddRenderable(*renderable, GetRenderQueueGroup(), 0);
			}
		}
	}

	void ParticleSystem::VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables)
	{
		for (auto& emitter : m_emitters)
		{
			if (emitter->IsMeshMode())
			{
				for (const auto& meshRenderable : emitter->GetMeshRenderables())
				{
					if (meshRenderable && meshRenderable->IsReady())
					{
						visitor.Visit(*meshRenderable, 0, false);
					}
				}
				continue;
			}

			ParticleRenderable* renderable = emitter->GetRenderable();
			if (renderable && renderable->IsReady())
			{
				visitor.Visit(*renderable, 0, false);
			}
		}
	}

	void ParticleSystem::UpdateBoundingBox()
	{
		if (m_emitters.empty())
		{
			m_boundingBox.min = Vector3::Zero;
			m_boundingBox.max = Vector3::Zero;
			return;
		}

		bool any = false;
		Vector3 minPos = Vector3::Zero;
		Vector3 maxPos = Vector3::Zero;

		for (const auto& emitter : m_emitters)
		{
			if (emitter->GetParticleCount() == 0)
			{
				continue;
			}

			const AABB& box = emitter->GetBoundingBox();
			if (!any)
			{
				minPos = box.min;
				maxPos = box.max;
				any = true;
			}
			else
			{
				minPos.x = std::min(minPos.x, box.min.x);
				minPos.y = std::min(minPos.y, box.min.y);
				minPos.z = std::min(minPos.z, box.min.z);
				maxPos.x = std::max(maxPos.x, box.max.x);
				maxPos.y = std::max(maxPos.y, box.max.y);
				maxPos.z = std::max(maxPos.z, box.max.z);
			}
		}

		if (any)
		{
			m_boundingBox.min = minPos;
			m_boundingBox.max = maxPos;
		}
		else
		{
			const Vector3 pos = GetDerivedPosition();
			m_boundingBox.min = pos;
			m_boundingBox.max = pos;
		}
	}

	MaterialPtr ParticleSystem::GetDefaultMaterial(bool additive)
	{
		static MaterialPtr s_additiveMaterial;
		static MaterialPtr s_alphaMaterial;
		static bool s_attempted = false;

		if (!s_attempted)
		{
			s_attempted = true;

			try
			{
				s_additiveMaterial = MaterialManager::Get().Load("Particles/Additive.hmat");
			}
			catch (...)
			{
				WLOG("Failed to load default additive particle material (Particles/Additive.hmat)");
			}

			try
			{
				s_alphaMaterial = MaterialManager::Get().Load("Particles/Alpha.hmat");
			}
			catch (...)
			{
				WLOG("Failed to load default alpha particle material (Particles/Alpha.hmat)");
			}
		}

		return additive ? s_additiveMaterial : s_alphaMaterial;
	}
}
