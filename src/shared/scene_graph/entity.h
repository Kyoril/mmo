#pragma once

#include "sub_entity.h"
#include "mesh.h"
#include "base/linear_set.h"

#include "scene_graph/movable_object.h"

namespace mmo
{
	class RenderQueue;

	class Entity : public MovableObject
	{
	public:

		typedef LinearSet<Entity*> EntitySet;

	protected:
		Entity();

		Entity(const String& name, MeshPtr mesh);

	public:
		[[nodiscard]] const MeshPtr& GetMesh() const noexcept { return m_mesh; }

		SubEntity* GetSubEntity(uint16 index) const noexcept;

		SubEntity* GetSubEntity(const String& name) const noexcept;

		uint32 GetNumSubEntities() const noexcept;
		
	public:
		/// @copydoc MovableObject::SetCurrentCamera
		virtual void SetCurrentCamera(Camera& cam) override;
		
		/// @copydoc MovableObject::UpdateRenderQueue
		virtual void UpdateRenderQueue(RenderQueue& renderQueue) override;

	protected:
		MeshPtr m_mesh;

		typedef std::vector<std::unique_ptr<SubEntity>> SubEntities;
		SubEntities m_subEntities;

		bool m_initialized { false };

		Matrix4 m_lastParentTransform;

		typedef std::map<String, MovableObject*> ChildObjects;
		ChildObjects m_childObjects;

		mutable AABB m_fullBoundingBox;
		
	protected:
		void BuildSubEntityList(const MeshPtr& mesh, SubEntities& subEntities);

		void Initialize();

		void Deinitialize();
	};
}
