#pragma once

#include "selectable.h"

#include <functional>

#include "proto_data/project.h"

namespace mmo
{
	class Entity;
	class SceneNode;

	namespace terrain
	{
		class Tile;
	}

	class MapEntity;

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
		SelectedObjectSpawn(proto::ObjectSpawnEntry& entry, const proto::ObjectManager& objects, const proto::ModelDataManager& models, SceneNode& node, Entity& entity,
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
		const proto::ModelDataManager& m_models;
		SceneNode& m_node;
		Entity& m_entity;
		std::function<void(Selectable&)> m_duplication;
		std::function<void(const proto::ObjectSpawnEntry&)> m_removal;
	};
}
