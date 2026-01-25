// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/signal.h"

#include <imgui.h>
#include <asio/io_service.hpp>

#include "selected_map_entity.h"
#include "paging/world_page_loader.h"
#include "base/id_generator.h"
#include "editors/editor_instance.h"
#include "graphics/render_texture.h"
#include "scene_graph/scene.h"
#include "scene_graph/mesh_serializer.h"
#include "scene_graph/world_grid.h"
#include "scene_graph/world_model.h"
#include "transform_widget.h"
#include "selection.h"

namespace mmo
{
	class WorldModelEditor;
	class Entity;
	class Camera;
	class SceneNode;
	class Light;

	/// @brief Represents a selectable group in the world model editor.
	class SelectedWorldModelGroup final : public Selectable
	{
	public:
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

	public:
		/// @brief Signal fired when this mesh is removed.
		signal<void()> removed;

	private:
		WorldModelGroup& m_group;
		size_t m_meshIndex;
		SceneNode& m_node;
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

	/// @brief Edit modes for the world model editor.
	enum class WorldModelEditMode
	{
		/// @brief Default mode - select and transform groups.
		Select,
		/// @brief Mode for editing portals between groups.
		Portal,
		/// @brief Mode for placing doodads within groups.
		Doodad,
		/// @brief Mode for placing lights within groups.
		Light,
		/// @brief Mode for defining collision geometry.
		Collision,
		/// @brief Mode for editing mesh references within groups.
		MeshEdit
	};

	/// @brief Represents visualization of a single mesh reference within a group.
	struct MeshRefVisualization
	{
		SceneNode* node { nullptr };
		Entity* entity { nullptr };
		MeshPtr mesh { nullptr };
		size_t meshRefIndex { 0 };
		bool visible { true };
	};

	/// @brief Represents a group visualization in the editor.
	struct GroupVisualization
	{
		SceneNode* node { nullptr };
		ManualRenderObject* boundingBoxRenderable { nullptr };
		
		// Legacy geometry (for old-style embedded geometry)
		Entity* meshEntity { nullptr };
		MeshPtr mesh { nullptr };
		
		// Mesh references (new style)
		std::vector<MeshRefVisualization> meshRefVisualizations;
		
		bool visible { true };
	};

	/// @brief Represents visualization of a child WMO reference.
	struct ChildWMOVisualization
	{
		SceneNode* node { nullptr };
		std::vector<Entity*> entities;
		size_t childRefIndex { 0 };
		bool visible { true };
	};

	/// @brief Represents a portal visualization in the editor.
	struct PortalVisualization
	{
		SceneNode* node { nullptr };
		ManualRenderObject* renderable { nullptr };
		int32 groupA { -1 };
		int32 groupB { -1 };
	};

	/// @brief Represents a light visualization in the editor.
	struct LightVisualization
	{
		SceneNode* node { nullptr };
		Light* light { nullptr };
		ManualRenderObject* iconRenderable { nullptr };
	};

	/// @brief Editor instance for editing world model objects (WMO-like structures).
	/// 
	/// World model objects are complex structures that can contain multiple groups (rooms),
	/// portals connecting groups for visibility culling, doodads, lights, and fog volumes.
	class WorldModelEditorInstance final : public EditorInstance, public ChunkReader
	{

	public:
		/// @brief Constructs the world model editor instance.
		/// @param host The editor host.
		/// @param editor The parent editor.
		/// @param asset The asset path to edit.
		explicit WorldModelEditorInstance(EditorHost& host, WorldModelEditor& editor, Path asset);

		/// @brief Destroys the world model editor instance.
		~WorldModelEditorInstance() override;

	public:
		/// @brief Renders the actual 3d viewport content.
		void Render();

		/// @copydoc EditorInstance::Draw
		void Draw() override;
		
		/// @copydoc EditorInstance::OnMouseButtonDown
		void OnMouseButtonDown(uint32 button, uint16 x, uint16 y) override;

		/// @copydoc EditorInstance::OnMouseButtonUp
		void OnMouseButtonUp(uint32 button, uint16 x, uint16 y) override;

		/// @copydoc EditorInstance::OnMouseMoved
		void OnMouseMoved(uint16 x, uint16 y) override;

		/// @copydoc EditorInstance::Save
		bool Save() override;

	private:
		/// @brief Updates the debug AABB visualization.
		void UpdateDebugAABB(const AABB& aabb);

		/// @brief Performs entity selection raycast.
		void PerformEntitySelectionRaycast(float viewportX, float viewportY);

		/// @brief Creates a map entity from a mesh.
		void CreateMapEntity(const String& assetName, const Vector3& position, const Quaternion& orientation, const Vector3& scale);

		/// @brief Called when a map entity is removed.
		void OnMapEntityRemoved(MapEntity& entity);

		/// @brief Creates a new group in the world model.
		/// @param name The name of the group.
		/// @param flags The group flags.
		/// @return Pointer to the created group.
		WorldModelGroup* CreateGroup(const String& name, uint32 flags = 0);

		/// @brief Removes a group from the world model.
		/// @param groupIndex The index of the group to remove.
		void RemoveGroup(size_t groupIndex);

		/// @brief Creates a portal between two groups.
		/// @param groupA First group index.
		/// @param groupB Second group index.
		/// @param vertices Portal vertices.
		void CreatePortal(int32 groupA, int32 groupB, const std::vector<Vector3>& vertices);

		/// @brief Removes a portal from the world model.
		/// @param portalIndex The index of the portal to remove.
		void RemovePortal(size_t portalIndex);

		/// @brief Creates a light in the world model.
		/// @param groupIndex The group to add the light to.
		/// @param position The light position.
		/// @param color The light color.
		/// @param intensity The light intensity.
		void CreateLight(int32 groupIndex, const Vector3& position, uint32 color, float intensity);

		/// @brief Removes a light from the world model.
		/// @param lightIndex The index of the light to remove.
		void RemoveLight(size_t lightIndex);

		/// @brief Creates a doodad set.
		/// @param name The name of the set.
		/// @return Index of the created set.
		uint32 CreateDoodadSet(const String& name);

		/// @brief Adds a doodad to the current set.
		/// @param setIndex The set index.
		/// @param meshPath The mesh path.
		/// @param position The position.
		/// @param rotation The rotation.
		/// @param scale The scale.
		void AddDoodad(uint32 setIndex, const String& meshPath, const Vector3& position, const Quaternion& rotation, float scale);

		/// @brief Assigns a mesh to a group for geometry (legacy).
		/// @param groupIndex The group index.
		/// @param meshPath The path to the mesh file.
		void AssignMeshToGroup(int32 groupIndex, const String& meshPath);

		/// @brief Adds a mesh reference to a group.
		/// @param groupIndex The group index.
		/// @param meshPath The path to the mesh file.
		/// @param position Local position within the group.
		/// @param rotation Local rotation within the group.
		/// @param scale Local scale.
		/// @param name Optional name for the mesh reference.
		void AddMeshRefToGroup(int32 groupIndex, const String& meshPath, 
			const Vector3& position = Vector3::Zero,
			const Quaternion& rotation = Quaternion::Identity,
			const Vector3& scale = Vector3::UnitScale,
			const String& name = "");

		/// @brief Removes a mesh reference from a group.
		/// @param groupIndex The group index.
		/// @param meshRefIndex The mesh reference index within the group.
		void RemoveMeshRefFromGroup(int32 groupIndex, size_t meshRefIndex);

		/// @brief Adds a child WMO reference.
		/// @param wmoPath The path to the child WMO file.
		/// @param position Position of the child WMO.
		/// @param rotation Rotation of the child WMO.
		/// @param scale Scale of the child WMO.
		/// @param name Optional name for the child reference.
		void AddChildWMO(const String& wmoPath,
			const Vector3& position = Vector3::Zero,
			const Quaternion& rotation = Quaternion::Identity,
			const Vector3& scale = Vector3::UnitScale,
			const String& name = "");

		/// @brief Removes a child WMO reference.
		/// @param childIndex The index of the child WMO to remove.
		void RemoveChildWMO(size_t childIndex);

		/// @brief Updates visualization for all groups.
		void UpdateGroupVisualizations();

		/// @brief Updates visualization for mesh references in a group.
		/// @param groupIndex The group index.
		void UpdateMeshRefVisualizations(size_t groupIndex);

		/// @brief Updates visualization for all child WMOs.
		void UpdateChildWMOVisualizations();

		/// @brief Updates visualization for all portals.
		void UpdatePortalVisualizations();

		/// @brief Updates visualization for all lights.
		void UpdateLightVisualizations();

		/// @brief Draws the groups panel UI.
		void DrawGroupsPanel();

		/// @brief Draws the mesh references panel UI for a group.
		/// @param groupIndex The group index.
		void DrawMeshRefsPanel(int32 groupIndex);

		/// @brief Draws the child WMOs panel UI.
		void DrawChildWMOsPanel();

		/// @brief Draws the portals panel UI.
		void DrawPortalsPanel();

		/// @brief Draws the doodads panel UI.
		void DrawDoodadsPanel();

		/// @brief Draws the lights panel UI.
		void DrawLightsPanel();

		/// @brief Draws the fog panel UI.
		void DrawFogPanel();

		/// @brief Draws the properties panel for the current selection.
		void DrawPropertiesPanel();

		/// @brief Draws the toolbar.
		void DrawToolbar();

	private:
		/// @brief Reads the version chunk.
		bool ReadMVERChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		/// @brief Reads the mesh names chunk.
		bool ReadMeshChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		/// @brief Reads an entity chunk.
		bool ReadEntityChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		/// @copydoc ChunkReader::OnReadFinished
		bool OnReadFinished() override;

	private:
		WorldModelEditor& m_editor;
		scoped_connection m_renderConnection;
		ImVec2 m_lastAvailViewportSize;
		RenderTexturePtr m_viewportRT;
		bool m_wireFrame;
		Scene m_scene;
		SceneNode* m_cameraAnchor { nullptr };
		SceneNode* m_cameraNode { nullptr };
		SceneNode* m_lightNode { nullptr };
		Light* m_mainLight { nullptr };
		Entity* m_entity { nullptr };
		Camera* m_camera { nullptr };
		std::unique_ptr<WorldGrid> m_worldGrid;
		int16 m_lastMouseX { 0 }, m_lastMouseY { 0 };
		bool m_leftButtonPressed { false };
		bool m_rightButtonPressed { false };
		bool m_initDockLayout { true };
		MeshPtr m_mesh;
		MeshEntry m_entry { };
		Vector3 m_cameraVelocity{};
		bool m_hovering{ false };

		bool m_gridSnap { true };
		float m_translateSnapSizes[7] = { 0.1f, 0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 4.0f };
		float m_rotateSnapSizes[6] = { 1.0f, 5.0f, 10.0f, 15.0f, 45.0f, 90.0f };
		int m_currentTranslateSnapSize { 3 };
		int m_currentRotateSnapSize { 3 };

		std::unique_ptr<RaySceneQuery> m_raySceneQuery;
			
		Selection m_selection;
		std::unique_ptr<TransformWidget> m_transformWidget;

		ManualRenderObject* m_debugBoundingBox;

		float m_cameraSpeed { 20.0f };
		IdGenerator<uint64> m_objectIdGenerator { 1 };

		std::vector<std::unique_ptr<MapEntity>> m_mapEntities;
		ImVec2 m_lastContentRectMin{};

		Light* m_sunLight{ nullptr };

		std::vector<String> m_meshNames;
		Vector3 m_brushPosition{};

		// World model specific members
		WorldModelPtr m_worldModel;
		WorldModelEditMode m_editMode { WorldModelEditMode::Select };
		int32 m_selectedGroupIndex { -1 };
		int32 m_selectedPortalIndex { -1 };
		int32 m_selectedLightIndex { -1 };
		int32 m_selectedDoodadSetIndex { 0 };

		std::vector<GroupVisualization> m_groupVisualizations;
		std::vector<PortalVisualization> m_portalVisualizations;
		std::vector<LightVisualization> m_lightVisualizations;
		uint32 m_meshCounter { 0 };

		bool m_showGroupBounds { true };
		bool m_showPortals { true };
		bool m_showLights { true };
		bool m_showDoodads { true };

		// Portal creation state
		bool m_creatingPortal { false };
		int32 m_portalSourceGroup { -1 };
		int32 m_portalTargetGroup { -1 };
		std::vector<Vector3> m_portalVertices;

		// New group dialog state
		bool m_showNewGroupDialog { false };
		char m_newGroupName[256] { "" };
		uint32 m_newGroupFlags { 0 };

		// Assign mesh dialog state
		bool m_showAssignMeshDialog { false };
		char m_assignMeshPath[512] { "" };

		// Child WMO visualizations
		std::vector<ChildWMOVisualization> m_childWMOVisualizations;
		
		// Mesh editing state
		int32 m_selectedMeshRefIndex { -1 };
		int32 m_selectedChildWMOIndex { -1 };
		
		// Add mesh ref dialog state
		bool m_showAddMeshRefDialog { false };
		char m_addMeshRefPath[512] { "" };
		char m_addMeshRefName[256] { "" };
		
		// Add child WMO dialog state
		bool m_showAddChildWMODialog { false };
		char m_addChildWMOPath[512] { "" };
		char m_addChildWMOName[256] { "" };
	};
}
