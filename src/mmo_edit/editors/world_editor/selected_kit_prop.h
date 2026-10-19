// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "selectable.h"

#include "base/typedefs.h"

namespace mmo
{
	class WorldEditorInstance;
	struct WorldModelMeshRef;

	/// @brief Selection handle for one prop of a prop kit applied to a placed world model.
	/// @details The handle only stores ids (placement, kit path, prop index) and resolves them through
	///          the world editor on every access, so it stays safe when the placement is unloaded or the
	///          kit is rebuilt. Transforms are exposed in world space and stored in building-local space.
	///          Every edit goes through the world editor, which marks the kit dirty and refreshes every
	///          loaded placement using that kit.
	class SelectedKitProp final : public Selectable
	{
	public:
		/// @brief Creates the selection handle.
		/// @param editor The world editor owning the placement and the kit working copies.
		/// @param placementId Unique id of the world model placement the prop was picked on.
		/// @param kitPath Asset path of the kit, as stored in the placement's kit list.
		/// @param propIndex Index of the prop in the kit's prop list.
		SelectedKitProp(WorldEditorInstance& editor, uint64 placementId, String kitPath, size_t propIndex);

	public:
		/// @copydoc Selectable::Visit
		void Visit(SelectableVisitor& visitor) override;

		/// @brief Appends a copy of the prop to the kit. The handle keeps editing the original.
		void Duplicate() override;

		/// @copydoc Selectable::Translate
		void Translate(const Vector3& delta) override;

		/// @copydoc Selectable::Rotate
		void Rotate(const Quaternion& delta) override;

		/// @copydoc Selectable::Scale
		void Scale(const Vector3& delta) override;

		/// @brief Queues the prop for removal from its kit (applied after the current frame's UI).
		void Remove() override;

		/// @copydoc Selectable::Deselect
		void Deselect() override {}

		/// @copydoc Selectable::SetPosition
		void SetPosition(const Vector3& position) const override;

		/// @copydoc Selectable::SetOrientation
		void SetOrientation(const Quaternion& orientation) const override;

		/// @copydoc Selectable::SetScale
		void SetScale(const Vector3& scale) const override;

		/// @copydoc Selectable::GetPosition
		[[nodiscard]] Vector3 GetPosition() const override;

		/// @copydoc Selectable::GetOrientation
		[[nodiscard]] Quaternion GetOrientation() const override;

		/// @copydoc Selectable::GetScale
		[[nodiscard]] Vector3 GetScale() const override;

	public:
		/// @brief Gets the unique id of the placement the prop belongs to.
		[[nodiscard]] uint64 GetPlacementId() const { return m_placementId; }

		/// @brief Gets the asset path of the kit the prop belongs to.
		[[nodiscard]] const String& GetKitPath() const { return m_kitPath; }

		/// @brief Gets the index of the prop in its kit.
		[[nodiscard]] size_t GetPropIndex() const { return m_propIndex; }

	private:
		/// @brief Gets the placement's world transform. Returns false when the placement is gone.
		bool GetPlacementTransform(Vector3& position, Quaternion& orientation, Vector3& scale) const;

		/// @brief Gets the current prop data (working copy or cached kit), or null when gone.
		[[nodiscard]] const WorldModelMeshRef* FindProp() const;

	private:
		WorldEditorInstance& m_editor;
		uint64 m_placementId;
		String m_kitPath;
		size_t m_propIndex;
	};
}
