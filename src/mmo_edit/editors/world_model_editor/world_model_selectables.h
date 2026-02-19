// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/signal.h"
#include "selection.h"
#include "scene_graph/world_model.h"

namespace mmo
{
	class SceneNode;

	/// @brief Represents a selectable group in the world model editor.
	class SelectedWorldModelGroup final : public Selectable
	{
	public:
		/// @brief Creates a selectable world model group.
		/// @param group The world model group to select.
		/// @param node The scene node representing this group.
		explicit SelectedWorldModelGroup(WorldModelGroup& group, SceneNode& node);

		/// @copydoc Selectable::Visit
		void Visit(SelectableVisitor& visitor) override {}

		/// @copydoc Selectable::GetPosition
		Vector3 GetPosition() const override;

		/// @copydoc Selectable::SetPosition
		void SetPosition(const Vector3& position) const override;

		/// @copydoc Selectable::GetOrientation
		Quaternion GetOrientation() const override;

		/// @copydoc Selectable::SetOrientation
		void SetOrientation(const Quaternion& orientation) const override;

		/// @copydoc Selectable::GetScale
		Vector3 GetScale() const override;

		/// @copydoc Selectable::SetScale
		void SetScale(const Vector3& scale) const override;

		/// @copydoc Selectable::Translate
		void Translate(const Vector3& delta) override;

		/// @copydoc Selectable::Rotate
		void Rotate(const Quaternion& delta) override;

		/// @copydoc Selectable::Scale
		void Scale(const Vector3& delta) override;

		/// @copydoc Selectable::SupportsTranslate
		bool SupportsTranslate() const override { return true; }

		/// @copydoc Selectable::SupportsRotate
		bool SupportsRotate() const override { return false; }

		/// @copydoc Selectable::SupportsScale
		bool SupportsScale() const override { return true; }

		/// @copydoc Selectable::Duplicate
		void Duplicate() override {}

		/// @copydoc Selectable::Remove
		void Remove() override;

		/// @copydoc Selectable::Deselect
		void Deselect() override {}

		/// @brief Gets the underlying world model group.
		WorldModelGroup& GetGroup() { return m_group; }

	public:
		/// @brief Signal fired when this group is removed.
		signal<void()> removed;

	private:
		WorldModelGroup& m_group;
		SceneNode& m_node;
	};

	/// @brief Represents a selectable mesh reference within a world model group.
	/// This allows selecting and transforming individual meshes within a group.
	class SelectedGroupMesh final : public Selectable
	{
	public:
		/// @brief Creates a selectable mesh reference.
		/// @param group The group containing the mesh.
		/// @param meshIndex The index of the mesh reference within the group.
		/// @param node The scene node representing this mesh.
		explicit SelectedGroupMesh(WorldModelGroup& group, size_t meshIndex, SceneNode& node);

		/// @copydoc Selectable::Visit
		void Visit(SelectableVisitor& visitor) override {}

		/// @copydoc Selectable::GetPosition
		Vector3 GetPosition() const override;

		/// @copydoc Selectable::SetPosition
		void SetPosition(const Vector3& position) const override;

		/// @copydoc Selectable::GetOrientation
		Quaternion GetOrientation() const override;

		/// @copydoc Selectable::SetOrientation
		void SetOrientation(const Quaternion& orientation) const override;

		/// @copydoc Selectable::GetScale
		Vector3 GetScale() const override;

		/// @copydoc Selectable::SetScale
		void SetScale(const Vector3& scale) const override;

		/// @copydoc Selectable::Translate
		void Translate(const Vector3& delta) override;

		/// @copydoc Selectable::Rotate
		void Rotate(const Quaternion& delta) override;

		/// @copydoc Selectable::Scale
		void Scale(const Vector3& delta) override;

		/// @copydoc Selectable::SupportsTranslate
		bool SupportsTranslate() const override { return true; }

		/// @copydoc Selectable::SupportsRotate
		bool SupportsRotate() const override { return true; }

		/// @copydoc Selectable::SupportsScale
		bool SupportsScale() const override { return true; }

		/// @copydoc Selectable::Duplicate
		void Duplicate() override;

		/// @copydoc Selectable::Remove
		void Remove() override;

		/// @copydoc Selectable::Deselect
		void Deselect() override {}

		/// @brief Gets the underlying world model group.
		WorldModelGroup& GetGroup() { return m_group; }

		/// @brief Gets the mesh index within the group.
		size_t GetMeshIndex() const { return m_meshIndex; }

		/// @brief Gets the mesh reference, or nullptr if invalid.
		WorldModelMeshRef* GetMeshRef();

		/// @brief Updates the scene node pointer after visualization rebuild.
		/// @param node The new scene node pointer.
		void UpdateNode(SceneNode* node) { m_node = node; }

	public:
		/// @brief Signal fired when this mesh is removed.
		signal<void()> removed;

		/// @brief Signal fired when this mesh is duplicated.
		/// @param newIndex The index of the newly created mesh reference.
		signal<void(size_t newIndex)> duplicated;

	private:
		WorldModelGroup& m_group;
		size_t m_meshIndex;
		SceneNode* m_node;
	};

	/// @brief Represents a selectable child WMO reference.
	/// This allows selecting and transforming child world models.
	class SelectedChildWMO final : public Selectable
	{
	public:
		/// @brief Creates a selectable child WMO reference.
		/// @param worldModel The parent world model.
		/// @param childIndex The index of the child reference.
		/// @param node The scene node representing this child WMO.
		explicit SelectedChildWMO(WorldModel& worldModel, size_t childIndex, SceneNode& node);

		/// @copydoc Selectable::Visit
		void Visit(SelectableVisitor& visitor) override {}

		/// @copydoc Selectable::GetPosition
		Vector3 GetPosition() const override;

		/// @copydoc Selectable::SetPosition
		void SetPosition(const Vector3& position) const override;

		/// @copydoc Selectable::GetOrientation
		Quaternion GetOrientation() const override;

		/// @copydoc Selectable::SetOrientation
		void SetOrientation(const Quaternion& orientation) const override;

		/// @copydoc Selectable::GetScale
		Vector3 GetScale() const override;

		/// @copydoc Selectable::SetScale
		void SetScale(const Vector3& scale) const override;

		/// @copydoc Selectable::Translate
		void Translate(const Vector3& delta) override;

		/// @copydoc Selectable::Rotate
		void Rotate(const Quaternion& delta) override;

		/// @copydoc Selectable::Scale
		void Scale(const Vector3& delta) override;

		/// @copydoc Selectable::SupportsTranslate
		bool SupportsTranslate() const override { return true; }

		/// @copydoc Selectable::SupportsRotate
		bool SupportsRotate() const override { return true; }

		/// @copydoc Selectable::SupportsScale
		bool SupportsScale() const override { return true; }

		/// @copydoc Selectable::Duplicate
		void Duplicate() override;

		/// @copydoc Selectable::Remove
		void Remove() override;

		/// @copydoc Selectable::Deselect
		void Deselect() override {}

		/// @brief Gets the parent world model.
		WorldModel& GetWorldModel() { return m_worldModel; }

		/// @brief Gets the child WMO index.
		size_t GetChildIndex() const { return m_childIndex; }

		/// @brief Gets the child reference, or nullptr if invalid.
		WorldModelChildRef* GetChildRef();

	public:
		/// @brief Signal fired when this child WMO is removed.
		signal<void()> removed;

	private:
		WorldModel& m_worldModel;
		size_t m_childIndex;
		SceneNode& m_node;
	};

	/// @brief Represents a selectable light within a world model.
	/// This allows selecting and transforming lights using the transform widget.
	class SelectedWorldModelLight final : public Selectable
	{
	public:
		/// @brief Creates a selectable light reference.
		/// @param worldModel The world model containing the light.
		/// @param lightIndex The index of the light within the world model.
		/// @param node The scene node representing this light.
		explicit SelectedWorldModelLight(WorldModel& worldModel, size_t lightIndex, SceneNode& node);

		/// @copydoc Selectable::Visit
		void Visit(SelectableVisitor& visitor) override {}

		/// @copydoc Selectable::GetPosition
		Vector3 GetPosition() const override;

		/// @copydoc Selectable::SetPosition
		void SetPosition(const Vector3& position) const override;

		/// @copydoc Selectable::GetOrientation
		Quaternion GetOrientation() const override;

		/// @copydoc Selectable::SetOrientation
		void SetOrientation(const Quaternion& orientation) const override;

		/// @copydoc Selectable::GetScale
		Vector3 GetScale() const override;

		/// @copydoc Selectable::SetScale
		void SetScale(const Vector3& scale) const override;

		/// @copydoc Selectable::Translate
		void Translate(const Vector3& delta) override;

		/// @copydoc Selectable::Rotate
		void Rotate(const Quaternion& delta) override;

		/// @copydoc Selectable::Scale
		void Scale(const Vector3& delta) override;

		/// @copydoc Selectable::SupportsTranslate
		bool SupportsTranslate() const override { return true; }

		/// @copydoc Selectable::SupportsRotate
		bool SupportsRotate() const override { return true; }

		/// @copydoc Selectable::SupportsScale
		bool SupportsScale() const override { return false; }

		/// @copydoc Selectable::SupportsDuplicate
		bool SupportsDuplicate() const override { return true; }

		/// @copydoc Selectable::Duplicate
		void Duplicate() override;

		/// @copydoc Selectable::Remove
		void Remove() override;

		/// @copydoc Selectable::Deselect
		void Deselect() override {}

		/// @brief Gets the parent world model.
		WorldModel& GetWorldModel() { return m_worldModel; }

		/// @brief Gets the light index.
		size_t GetLightIndex() const { return m_lightIndex; }

		/// @brief Sets the light index (used after selection switches to duplicate).
		void SetLightIndex(size_t index) { m_lightIndex = index; }

		/// @brief Gets the light data, or nullptr if invalid.
		WorldModelLight* GetLight();

		/// @brief Updates the scene node pointer after visualization rebuild.
		/// @param node The new scene node pointer.
		void UpdateNode(SceneNode* node) { m_node = node; }

	public:
		/// @brief Signal fired when this light is removed.
		signal<void()> removed;

		/// @brief Signal fired when this light is duplicated.
		/// @param newIndex The index of the newly created light.
		signal<void(size_t newIndex)> duplicated;

	private:
		WorldModel& m_worldModel;
		size_t m_lightIndex;
		SceneNode* m_node;
	};

}
