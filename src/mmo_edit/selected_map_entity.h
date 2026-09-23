#pragma once

#include "selectable.h"

#include <functional>
#include <vector>

#include "proto_data/project.h"
#include "game_common/fog_volume.h"

namespace mmo
{
	class Entity;
	class SceneNode;
	class ManualRenderObject;

	namespace terrain
	{
		class Tile;
	}

	class MapEntity;
	class IWorldEditor;

	class SelectedMapEntity : public Selectable
	{
	public:
		SelectedMapEntity(MapEntity& entity, const std::function<void(Selectable&)>& duplication);

	public:
		// Inherited via Selectable
		void Translate(const Vector3& delta) override;
		void Rotate(const Quaternion& delta) override;
		void Scale(const Vector3& delta) override;
		void Remove() override;
		void Deselect() override;
		Vector3 GetPosition() const override;
		Quaternion GetOrientation() const override;
		Vector3 GetScale() const override;
		void SetPosition(const Vector3& position) const override;
		void SetOrientation(const Quaternion& orientation) const override;
		void SetScale(const Vector3& scale) const override;
		void Duplicate() override;

	public:
		MapEntity& GetEntity() const { return m_entity; }
		void Visit(SelectableVisitor& visitor) override;

	private:
		MapEntity& m_entity;
		std::function<void(Selectable&)> m_duplication;
	};

	class SelectedTerrainTile final : public Selectable
	{
	public:
		SelectedTerrainTile(terrain::Tile& tile);

	public:
		// Inherited via Selectable
		void Visit(SelectableVisitor& visitor) override;

		void Translate(const Vector3& delta) override {}

		void Rotate(const Quaternion& delta) override {}

		void Scale(const Vector3& delta) override {}

		void Remove() override {}

		void Deselect() override;

		Vector3 GetPosition() const override;

		Quaternion GetOrientation() const override;

		Vector3 GetScale() const override;

		void SetPosition(const Vector3& position) const override {}

		void SetOrientation(const Quaternion& orientation) const override {}

		void SetScale(const Vector3& scale) const override {}

		void Duplicate() override {}

		bool SupportsTranslate() const override { return false; }

		bool SupportsRotate() const override { return false; }

		bool SupportsScale() const override { return false; }

		bool SupportsDuplicate() const override { return false; }

	public:
		terrain::Tile& GetTile() const { return m_tile; }

	private:
		terrain::Tile& m_tile;
	};

	class SelectedUnitSpawn final : public Selectable
	{
	public:
		SelectedUnitSpawn(proto::UnitSpawnEntry& entry, const proto::UnitManager& units, const proto::ModelDataManager& models, SceneNode& node, Entity& entity, 
			const std::function<void(Selectable&)>& duplication, const std::function<void(const proto::UnitSpawnEntry&)>& removal);

		void Visit(SelectableVisitor& visitor) override;

		void Duplicate() override;

		void Translate(const Vector3& delta) override;

		void Rotate(const Quaternion& delta) override;

		void Scale(const Vector3& delta) override;

		void Remove() override;

		void Deselect() override;

		void SetPosition(const Vector3& position) const override;

		void SetOrientation(const Quaternion& orientation) const override;

		void SetScale(const Vector3& scale) const override;

		Vector3 GetPosition() const override;

		Quaternion GetOrientation() const override;

		Vector3 GetScale() const override;

		bool SupportsScale() const override { return false; }

		void RefreshEntity();

		proto::UnitSpawnEntry& GetEntry() const { return m_entry; }

	private:
		proto::UnitSpawnEntry& m_entry;
		const proto::UnitManager& m_units;
		const proto::ModelDataManager& m_models;
		SceneNode& m_node;
		Entity& m_entity;
		std::function<void(Selectable&)> m_duplication;
		std::function<void(const proto::UnitSpawnEntry&)> m_removal;
	};


	class SelectedObjectSpawn final : public Selectable
	{
	public:
		SelectedObjectSpawn(proto::ObjectSpawnEntry& entry, const proto::ObjectManager& objects, const proto::ObjectDisplayManager& models, SceneNode& node, Entity& entity,
			const std::function<void(Selectable&)>& duplication, const std::function<void(const proto::ObjectSpawnEntry&)>& removal);

		void Visit(SelectableVisitor& visitor) override;

		void Duplicate() override;

		void Translate(const Vector3& delta) override;

		void Rotate(const Quaternion& delta) override;

		void Scale(const Vector3& delta) override;

		void Remove() override;

		void Deselect() override;

		void SetPosition(const Vector3& position) const override;

		void SetOrientation(const Quaternion& orientation) const override;

		void SetScale(const Vector3& scale) const override;

		Vector3 GetPosition() const override;

		Quaternion GetOrientation() const override;

		Vector3 GetScale() const override;

		bool SupportsScale() const override { return false; }

		void RefreshEntity();

		proto::ObjectSpawnEntry& GetEntry() const { return m_entry; }

	private:
		proto::ObjectSpawnEntry& m_entry;
		const proto::ObjectManager& m_units;
		const proto::ObjectDisplayManager& m_models;
		SceneNode& m_node;
		Entity& m_entity;
		std::function<void(Selectable&)> m_duplication;
		std::function<void(const proto::ObjectSpawnEntry&)> m_removal;
	};

	class SelectedAreaTrigger final : public Selectable
	{
	public:
		SelectedAreaTrigger(proto::AreaTriggerEntry& entry, SceneNode& node, ManualRenderObject& renderObject,
			const std::function<void(Selectable&)>& duplication, const std::function<void(const proto::AreaTriggerEntry&)>& removal);

		void Visit(SelectableVisitor& visitor) override;

		void Duplicate() override;

		void Translate(const Vector3& delta) override;

		void Rotate(const Quaternion& delta) override;

		void Scale(const Vector3& delta) override;

		void Remove() override;

		void Deselect() override;

		void SetPosition(const Vector3& position) const override;

		void SetOrientation(const Quaternion& orientation) const override;

		void SetScale(const Vector3& scale) const override;

		Vector3 GetPosition() const override;

		Quaternion GetOrientation() const override;

		Vector3 GetScale() const override;

		bool SupportsDuplicate() const override { return false; }

		void RefreshVisual();

		proto::AreaTriggerEntry& GetEntry() const { return m_entry; }

	private:
		proto::AreaTriggerEntry& m_entry;
		SceneNode& m_node;
		ManualRenderObject& m_renderObject;
		std::function<void(Selectable&)> m_duplication;
		std::function<void(const proto::AreaTriggerEntry&)> m_removal;
	};

	/// @brief Rebuilds the editor wireframe of a fog volume into the given render object.
	/// @details The wireframe is built in the volume's local space (centred on the origin, unrotated),
	///          so the owning scene node must be positioned at `volume.position` and oriented with
	///          `Quaternion(Degree(volume.yaw), Vector3::UnitY)` - the same transform the renderer
	///          uses. Boxes draw their 12 edges, ellipsoids three axis-aligned rings. `volume.size`
	///          holds the full extents, so everything is drawn at half the size. Lines are tinted
	///          with the volume's colour (clamped to [0, 1]).
	/// @param renderObject The render object whose previous geometry is replaced.
	/// @param volume The fog volume to visualize.
	void BuildFogVolumeWireframe(ManualRenderObject& renderObject, const FogVolume& volume);

	/// @brief Extracts the rotation about +Y (in degrees, wrapped to [0, 360)) from an orientation.
	/// @details Measured by rotating +X and projecting it onto the XZ plane, which inverts
	///          `Quaternion(Degree(yaw), Vector3::UnitY)` exactly and ignores any pitch or roll.
	/// @param orientation The orientation to read the yaw from.
	/// @return The yaw in degrees within [0, 360).
	float ExtractFogVolumeYawDegrees(const Quaternion& orientation);

	/// @brief Wraps an angle in degrees into [0, 360).
	/// @param degrees The angle to wrap.
	/// @return The wrapped angle.
	float WrapFogVolumeYaw(float degrees);

	/// @brief A selected local fog volume in the world editor.
	/// @details Holds the volume's id rather than a pointer, because the editor's volume vector can
	///          reallocate when volumes are added. Every accessor looks the volume up by id, and every
	///          mutation flags the editor's fog volumes as changed so the next save writes them.
	class SelectedFogVolume final : public Selectable
	{
	public:
		/// @brief Creates a new selection wrapper for a fog volume.
		/// @param volumeId Id of the selected volume.
		/// @param volumes The editor's fog volume list the id refers into.
		/// @param worldEditor The owning world editor, used to flag unsaved changes.
		/// @param node The scene node carrying the volume's wireframe.
		/// @param renderObject The wireframe render object attached to the node.
		/// @param duplication Called to duplicate the selected volume (Alt + transform).
		/// @param removal Called with the volume id to delete the volume and its visual.
		SelectedFogVolume(uint32 volumeId, std::vector<FogVolume>& volumes, IWorldEditor& worldEditor, SceneNode& node, ManualRenderObject& renderObject,
			const std::function<void(Selectable&)>& duplication, const std::function<void(uint32)>& removal);

	public:
		/// @copydoc Selectable::Visit
		void Visit(SelectableVisitor& visitor) override;

		/// @copydoc Selectable::Duplicate
		void Duplicate() override;

		/// @brief Moves the volume by the given world-space offset.
		void Translate(const Vector3& delta) override;

		/// @brief Adds the rotation about +Y contained in `delta` to the volume's yaw.
		void Rotate(const Quaternion& delta) override;

		/// @brief Multiplies the volume's size per axis by `delta`, keeping every axis >= 0.5.
		void Scale(const Vector3& delta) override;

		/// @copydoc Selectable::Remove
		void Remove() override;

		/// @copydoc Selectable::Deselect
		void Deselect() override;

		/// @brief Sets the volume's world-space centre.
		void SetPosition(const Vector3& position) const override;

		/// @brief Sets the volume's yaw from the rotation about +Y contained in `orientation`.
		void SetOrientation(const Quaternion& orientation) const override;

		/// @brief Sets the volume's full extents (each axis >= 0.5).
		void SetScale(const Vector3& scale) const override;

		/// @brief Gets the volume's world-space centre.
		Vector3 GetPosition() const override;

		/// @brief Gets the volume's orientation, `Quaternion(Degree(yaw), Vector3::UnitY)`.
		Quaternion GetOrientation() const override;

		/// @brief Gets the volume's full extents.
		Vector3 GetScale() const override;

		/// @brief Gets the id of the selected volume.
		uint32 GetVolumeId() const { return m_volumeId; }

		/// @brief Fires the position/rotation/scale changed signals, so the transform widget follows
		///        edits made outside of the widget (for example in the details panel).
		void NotifyTransformChanged();

	private:
		/// @brief Looks up the selected volume, or nullptr if it no longer exists.
		FogVolume* FindVolume() const;

		/// @brief Moves and orients the scene node to match the volume.
		void SyncNode(const FogVolume& volume) const;

	private:
		uint32 m_volumeId;
		std::vector<FogVolume>& m_volumes;
		IWorldEditor& m_worldEditor;
		SceneNode& m_node;
		ManualRenderObject& m_renderObject;
		std::function<void(Selectable&)> m_duplication;
		std::function<void(uint32)> m_removal;
	};
}
