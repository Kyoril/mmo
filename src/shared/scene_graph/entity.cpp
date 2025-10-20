// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "entity.h"

#include "animation_state.h"
#include "scene_graph/render_queue.h"
#include "scene_graph/scene_node.h"
#include "skeleton_instance.h"
#include "tag_point.h"
#include "math/capsule.h"
#include "math/collision.h"

#include <algorithm>

#include "math/ray.h"

namespace mmo
{
	Entity::Entity()
		: MovableObject()
	{
		TODO("Implement");
	}

	Entity::Entity(const String& name, MeshPtr mesh)
		: MovableObject(name)
		, m_mesh(std::move(mesh))
	{
		Initialize();
	}

	void Entity::ResetSubEntities()
	{
		if (!m_mesh)
		{
			return;
		}

		uint16 index = 0;

		for (const auto& subEntity : m_subEntities)
		{
			subEntity->SetVisible(m_mesh->GetSubMesh(index).IsVisibleByDefault());
			subEntity->SetMaterial(m_mesh->GetSubMesh(index).GetMaterial());
			index++;
		}
	}

	SubEntity* Entity::GetSubEntity(const uint16 index) const
	{
		ASSERT(index <= m_subEntities.size());
		return m_subEntities[index].get();
	}

	SubEntity* Entity::GetSubEntity(const String& name) const
	{
		if (!m_mesh) return nullptr;

		const int32 index = m_mesh->GetSubMeshIndex(name);
		if (index < 0)
		{
			return nullptr;
		}

		return GetSubEntity(static_cast<uint16>(index));
	}

	uint32 Entity::GetNumSubEntities() const
	{
		return static_cast<uint32>(m_subEntities.size());
	}

	AnimationState* Entity::GetAnimationState(const String& name) const
	{
		if (!m_animationStates)
		{
			return nullptr;
		}

		return m_animationStates->GetAnimationState(name);
	}

	bool Entity::HasAnimationState(const String& name) const
	{
		if (!m_animationStates)
		{
			return false;
		}

		return m_animationStates->HasAnimationState(name);
	}

	AnimationStateSet* Entity::GetAllAnimationStates() const
	{
		return m_animationStates.get();
	}

	void Entity::SetMesh(MeshPtr mesh)
	{
		m_initialized = false;

		m_animationStates.reset();
		m_skeleton.reset();
		m_boneMatrices.clear();
		m_subEntities.clear();

		m_mesh = mesh;

		// Invalidate animation cache since we're changing the mesh/skeleton
		InvalidateAnimationCache();

		Initialize();
	}

	void Entity::PopulateRenderQueue(RenderQueue& renderQueue)
	{
		for (const auto& subEntity : m_subEntities)
		{
			if (!subEntity->IsVisible())
			{
				continue;
			}

			uint8 renderQueueGroup = GetRenderQueueGroup();
			if (subEntity->GetMaterial() && subEntity->GetMaterial()->IsTranslucent())
			{
				renderQueueGroup += 10;
			}

			renderQueue.AddRenderable(*subEntity, renderQueueGroup);
		}

		if (HasSkeleton())
		{
			UpdateAnimations();

			for (const auto& childIt : m_childObjects)
			{
				if (const bool visible = childIt.second->ShouldBeVisible())
				{
					childIt.second->PopulateRenderQueue(renderQueue);
				}
			}
		}
	}

	void Entity::SetMaterial(const MaterialPtr& material)
	{
		for (auto& subEntity : m_subEntities)
		{
			subEntity->SetMaterial(material);
		}
	}

	TagPoint* Entity::AttachObjectToBone(const String& boneName, MovableObject& pMovable, const Quaternion& offsetOrientation, const Vector3& offsetPosition)
	{
		ASSERT(!m_childObjects.contains(pMovable.GetName()));
		ASSERT(!pMovable.IsAttached());
		ASSERT(HasSkeleton());

		Bone* bone = m_skeleton->GetBone(boneName);
		ASSERT(bone);

		TagPoint* tp = m_skeleton->CreateTagPointOnBone(*bone, offsetOrientation, offsetPosition);
		tp->SetParentEntity(this);
		tp->SetChildObject(&pMovable);

		AttachObjectImpl(pMovable, *tp);

		// Trigger update of bounding box if necessary
		if (m_parentNode)
		{
			m_parentNode->NeedUpdate();
		}
		
		return tp;
	}

	MovableObject* Entity::DetachObjectFromBone(const String& movableName)
	{
		if (const auto it = m_childObjects.find(movableName); it != m_childObjects.end())
		{
			MovableObject* obj = it->second;
			DetachObjectImpl(*obj);
			m_childObjects.erase(it);

			if (m_parentNode)
			{
				m_parentNode->NeedUpdate();
			}

			return obj;
		}

		return nullptr;
	}

	void Entity::DetachObjectFromBone(const MovableObject& obj)
	{
		if (const auto it = m_childObjects.find(obj.GetName()); it != m_childObjects.end())
		{
			DetachObjectImpl(*it->second);
			m_childObjects.erase(it);

			if (m_parentNode)
			{
				m_parentNode->NeedUpdate();
			}
		}
	}

	void Entity::DetachAllObjectsFromBone()
	{
		DetachAllObjectsImpl();

		if (m_parentNode)
		{
			m_parentNode->NeedUpdate();
		}
	}
	
	void Entity::UpdateAnimations()
	{
		ASSERT(m_skeleton);
		ASSERT(m_animationStates);

		// Check if animations have been updated this frame already
		const uint64 currentAnimationFrame = m_animationStates->GetDirtyFrameNumber();
		if (m_lastAnimationUpdateFrame == currentAnimationFrame && !m_animationsNeedUpdate)
		{
			// Animations are already up to date for this frame
			return;
		}

		// Apply animation states
		m_skeleton->SetAnimationState(*m_animationStates);

		if (m_boneMatrices.size() != 256)
		{
			m_boneMatrices.resize(256, Matrix4::Identity);
			m_boneMatrixBuffer = GraphicsDevice::Get().CreateConstantBuffer(sizeof(Matrix4) * 256, m_boneMatrices.data());
		}

		m_skeleton->GetBoneMatrices(m_boneMatrices.data());
		m_boneMatrixBuffer->Update(m_boneMatrices.data());

		// Update cache information
		m_lastAnimationUpdateFrame = currentAnimationFrame;
		m_animationsNeedUpdate = false;
	}

	void Entity::AttachObjectImpl(MovableObject& pMovable, TagPoint& pAttachingPoint)
	{
		assert(!m_childObjects.contains(pMovable.GetName()));

		m_childObjects[pMovable.GetName()] = &pMovable;
		pMovable.NotifyAttachmentChanged(&pAttachingPoint, true);
	}

	void Entity::DetachObjectImpl(MovableObject& pObject) const
	{
		auto tp = static_cast<TagPoint*>(pObject.GetParentNode());

		// free the TagPoint so we can reuse it later
		m_skeleton->FreeTagPoint(*tp);
		pObject.NotifyAttachmentChanged(nullptr);
	}

	void Entity::DetachAllObjectsImpl()
	{
		for (auto& [name, obj] : m_childObjects)
		{
			DetachObjectImpl(*obj);
		}

		m_childObjects.clear();
	}

	void Entity::SetCurrentCamera(Camera& cam)
	{
		MovableObject::SetCurrentCamera(cam);

		if (m_parentNode)
		{
			// TODO: LoD level computation
		}
	}

	void Entity::BuildSubEntityList(const MeshPtr& mesh, SubEntities& subEntities)
	{
		const uint16 numSubMeshes = mesh->GetSubMeshCount();

		for (uint16 i = 0; i < numSubMeshes; ++i)
		{
			auto& subMesh = m_mesh->GetSubMesh(i);
			subEntities.emplace_back(std::make_unique<SubEntity>(*this, subMesh));
		}
	}

	void Entity::Initialize()
	{
		if (m_initialized)
		{
			return;
		}
        
        if (!m_mesh)
        {
            return;
        }

		m_boneMatrices.clear();

		BuildSubEntityList(m_mesh, m_subEntities);

		if (m_mesh->HasSkeleton())
		{
			m_skeleton = std::make_shared<SkeletonInstance>(m_mesh->GetSkeleton());
			m_animationStates = std::make_shared<AnimationStateSet>();
			m_skeleton->InitAnimationState(*m_animationStates);

			m_skeleton->Load();

			// Invalidate animation cache since we're initializing new animations
			InvalidateAnimationCache();
		}

		if (m_parentNode)
		{
			m_parentNode->NeedUpdate();
		}

		m_initialized = true;
	}

	void Entity::DeInitialize()
	{
		if (!m_initialized)
		{
			return;
		}

		m_skeleton = nullptr;

		// TODO: Extra cleanup
		m_childObjects.clear();
		
		m_initialized = false;
	}

	const String& Entity::GetMovableType() const
	{
		static String EntityType = "Entity";
		return EntityType;
	}

	const AABB& Entity::GetBoundingBox() const
	{
		ASSERT(m_mesh);
		
		// TODO: For now, return mesh bounds. This needs to be fixed to handle transforms properly
		// The real issue is that scaled entities need world-space bounds for proper octree queries
		return m_mesh->GetBounds();
	}

	float Entity::GetBoundingRadius() const
	{
		ASSERT(m_mesh);
		return m_mesh->GetBoundRadius();
	}

	void Entity::VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables)
	{
		for(const auto& subEntity : m_subEntities)
		{
			visitor.Visit(*subEntity, 0, false);
		}
	}

	bool Entity::TestCapsuleCollision(const Capsule& capsule, std::vector<CollisionResult>& results) const
	{
		ASSERT(IsCollidable());

		const auto& collisionTree = m_mesh->GetCollisionTree();
		if (collisionTree.IsEmpty())
		{
			return false;
		}

		// For non-uniform scaling, it's more accurate to transform mesh vertices to world space
		// rather than trying to handle ellipsoid-shaped capsules in local space
		const Matrix4 worldTransform = GetParentNodeFullTransform();
		
		// Keep capsule in world space - no transformation needed

		// Use stack-based traversal for optimal performance
		struct StackEntry
		{
			uint32 nodeIndex;
		};

		constexpr int maxStackSize = 64;
		StackEntry stack[maxStackSize];
		int stackCount = 1;
		stack[0] = { 0 }; // Start with root node

		bool foundCollision = false;

		const auto& nodes = collisionTree.GetNodes();
		const auto& vertices = collisionTree.GetVertices();
		const auto& indices = collisionTree.GetIndices();

		while (stackCount > 0 && stackCount < maxStackSize)
		{
			// Pop node from stack
			const StackEntry current = stack[--stackCount];
			
			if (current.nodeIndex >= nodes.size())
			{
				continue;
			}

			const auto& node = nodes[current.nodeIndex];

			// Transform node bounds to world space for AABB test
			// For non-uniform scaling, we need to transform all 8 corners and rebuild the AABB
			const Vector3& localMin = node.bounds.min;
			const Vector3& localMax = node.bounds.max;
			
			// Transform all 8 corners of the local AABB
			Vector3 corners[8] = {
				worldTransform * Vector3(localMin.x, localMin.y, localMin.z),
				worldTransform * Vector3(localMax.x, localMin.y, localMin.z),
				worldTransform * Vector3(localMin.x, localMax.y, localMin.z),
				worldTransform * Vector3(localMax.x, localMax.y, localMin.z),
				worldTransform * Vector3(localMin.x, localMin.y, localMax.z),
				worldTransform * Vector3(localMax.x, localMin.y, localMax.z),
				worldTransform * Vector3(localMin.x, localMax.y, localMax.z),
				worldTransform * Vector3(localMax.x, localMax.y, localMax.z)
			};
			
			// Find the world space AABB that contains all transformed corners
			AABB worldBounds;
			worldBounds.min = corners[0];
			worldBounds.max = corners[0];
			for (int i = 1; i < 8; ++i)
			{
				worldBounds.min.x = std::min(worldBounds.min.x, corners[i].x);
				worldBounds.min.y = std::min(worldBounds.min.y, corners[i].y);
				worldBounds.min.z = std::min(worldBounds.min.z, corners[i].z);
				worldBounds.max.x = std::max(worldBounds.max.x, corners[i].x);
				worldBounds.max.y = std::max(worldBounds.max.y, corners[i].y);
				worldBounds.max.z = std::max(worldBounds.max.z, corners[i].z);
			}

			// Test capsule against transformed node AABB - skip if no intersection
			if (!capsule.GetBounds().Intersects(worldBounds))
			{
				continue;
			}

			if (node.numFaces > 0)
			{
				// Leaf node - test against triangles
				for (uint32 i = 0; i < node.numFaces; ++i)
				{
					const uint32 faceIndex = node.startFace + i;
					if (faceIndex * 3 + 2 >= indices.size())
					{
						continue;
					}

					// Get triangle vertices
					const uint32 i0 = indices[faceIndex * 3 + 0];
					const uint32 i1 = indices[faceIndex * 3 + 1];
					const uint32 i2 = indices[faceIndex * 3 + 2];

					if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
					{
						continue;
					}

					const auto localV0 = Vector3(vertices[i0].x, vertices[i0].y, vertices[i0].z);
					const auto localV1 = Vector3(vertices[i1].x, vertices[i1].y, vertices[i1].z);
					const auto localV2 = Vector3(vertices[i2].x, vertices[i2].y, vertices[i2].z);

					// Transform triangle vertices to world space
					const Vector3 worldV0 = worldTransform * localV0;
					const Vector3 worldV1 = worldTransform * localV1;
					const Vector3 worldV2 = worldTransform * localV2;

					// Test capsule against triangle
					Vector3 contactPoint, contactNormal;
					float penetrationDepth;
					float distance;

					if (CapsuleTriangleIntersection(capsule, worldV0, worldV1, worldV2, contactPoint, contactNormal, penetrationDepth, distance))
					{
						results.emplace_back(true, contactPoint, contactNormal, worldV0, worldV1, worldV2, penetrationDepth, distance);
						foundCollision = true;
					}
				}
			}
			else
			{
				// Internal node - add children to stack if they exist and we have space
				if (stackCount < maxStackSize - 2 && node.children < nodes.size() - 1)
				{
					stack[stackCount++] = { node.children };     // Left child
					stack[stackCount++] = { node.children + 1 }; // Right child
				}
			}
		}

		return foundCollision;
	}

	bool Entity::TestRayCollision(const Ray& ray, CollisionResult& result) const
	{
		ASSERT(IsCollidable());

		const auto& collisionTree = m_mesh->GetCollisionTree();
		if (collisionTree.IsEmpty())
		{
			return false;
		}

		// Transform ray from world space to local space
		const Matrix4 worldTransform = GetParentNodeFullTransform();
		const Matrix4 invWorldTransform = worldTransform.Inverse();

		const Vector3 localOrigin = invWorldTransform * ray.origin;
		Vector3 localDirection = invWorldTransform * (ray.origin + ray.GetDirection()) - localOrigin;
		localDirection.Normalize();

		const Ray localRay(localOrigin, localOrigin + localDirection);

		// Use stack-based traversal for optimal performance
		struct StackEntry
		{
			uint32 nodeIndex;
		};

		constexpr int maxStackSize = 64;
		StackEntry stack[maxStackSize];
		int stackCount = 1;
		stack[0] = { 0 }; // Start with root node

		bool foundCollision = false;
		float closestDistance = std::numeric_limits<float>::max();
		Vector3 closestContactPoint;
		Vector3 closestContactNormal;

		const auto& nodes = collisionTree.GetNodes();
		const auto& vertices = collisionTree.GetVertices();
		const auto& indices = collisionTree.GetIndices();

		while (stackCount > 0 && stackCount < maxStackSize)
		{
			// Pop node from stack
			const StackEntry current = stack[--stackCount];

			if (current.nodeIndex >= nodes.size())
			{
				continue;
			}

			const auto& node = nodes[current.nodeIndex];

			// Test ray against node AABB in local space
			auto [intersects, distance] = localRay.IntersectsAABB(node.bounds);
			if (!intersects || distance > closestDistance)
			{
				continue;
			}

			if (node.numFaces > 0)
			{
				// Leaf node - test against triangles
				for (uint32 i = 0; i < node.numFaces; ++i)
				{
					const uint32 faceIndex = node.startFace + i;
					if (faceIndex * 3 + 2 >= indices.size())
					{
						continue;
					}

					// Get triangle vertices
					const uint32 i0 = indices[faceIndex * 3 + 0];
					const uint32 i1 = indices[faceIndex * 3 + 1];
					const uint32 i2 = indices[faceIndex * 3 + 2];

					if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
					{
						continue;
					}

					const auto v0 = Vector3(vertices[i0].x, vertices[i0].y, vertices[i0].z);
					const auto v1 = Vector3(vertices[i1].x, vertices[i1].y, vertices[i1].z);
					const auto v2 = Vector3(vertices[i2].x, vertices[i2].y, vertices[i2].z);

					// Test ray against triangle in local space
					auto [hitTriangle, hitDistance] = localRay.IntersectsTriangle(v0, v1, v2);

					if (hitTriangle && hitDistance < closestDistance)
					{
						closestDistance = hitDistance;

						// Calculate hit point in local space
						Vector3 localHitPoint = localRay.origin + localRay.GetDirection() * hitDistance;

						// Calculate triangle normal in local space
						Vector3 edge1 = v1 - v0;
						Vector3 edge2 = v2 - v0;
						Vector3 localNormal = edge1.Cross(edge2);
						localNormal.Normalize();

						// Transform hit point and normal to world space
						closestContactPoint = worldTransform * localHitPoint;
						closestContactNormal = worldTransform * localNormal - worldTransform * Vector3::Zero;
						closestContactNormal.Normalize();

						foundCollision = true;
					}
				}
			}
			else
			{
				// Internal node - add children to stack if they exist and we have space
				if (stackCount < maxStackSize - 2 && node.children < nodes.size() - 1)
				{
					stack[stackCount++] = { node.children };     // Left child
					stack[stackCount++] = { node.children + 1 }; // Right child
				}
			}
		}

		if (foundCollision)
		{
			result.hasCollision = true;
			result.contactPoint = closestContactPoint;
			result.contactNormal = closestContactNormal;
			result.penetrationDepth = closestDistance; // Distance along ray to hit point
		}

		return foundCollision;
	}
}
