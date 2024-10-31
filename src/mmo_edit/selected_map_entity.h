#pragma once

#include "selectable.h"

#include <functional>

namespace mmo
{
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
}