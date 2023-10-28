#pragma once

#include "selectable.h"

namespace mmo
{
	class MapEntity;

	class SelectedMapEntity : public Selectable
	{
	public:
		SelectedMapEntity(MapEntity& entity);

	public:
		// Inherited via Selectable
		virtual void Translate(const Vector3& delta) override;
		virtual void Rotate(const Quaternion& delta) override;
		virtual void Scale(const Vector3& delta) override;
		virtual void Remove() override;
		virtual void Deselect() override;
		virtual Vector3 GetPosition() const override;
		virtual Quaternion GetOrientation() const override;
		virtual Vector3 GetScale() const override;

	private:
		MapEntity& m_entity;
	};
}