// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_edit_mode.h"

namespace mmo
{
	class Vector3;

	/// @brief World editor mode for placing and editing client-only local fog volumes.
	/// @details While active, every volume of the open map gets a wireframe in the viewport, which
	///          can be clicked to select it and moved, rotated (about +Y) and scaled with the
	///          transform widget. The details panel lists the volumes and edits the selected one.
	class FogVolumeEditMode final : public WorldEditMode
	{
	public:
		/// @brief Creates the fog volume edit mode.
		/// @param worldEditor The world editor owning the fog volumes.
		explicit FogVolumeEditMode(IWorldEditor& worldEditor);

		~FogVolumeEditMode() override = default;

	public:
		/// @copydoc WorldEditMode::GetName
		const char* GetName() const override;

		/// @copydoc WorldEditMode::DrawDetails
		void DrawDetails() override;

		/// @brief Creates the wireframes of every fog volume.
		void OnActivate() override;

		/// @brief Clears the selection and destroys the fog volume wireframes.
		void OnDeactivate() override;

		/// @brief Picks the fog volume under the given viewport position.
		/// @param viewportX Normalized viewport X coordinate (0-1).
		/// @param viewportY Normalized viewport Y coordinate (0-1).
		/// @return Id of the closest volume hit by the view ray, or 0 if none was hit.
		uint32 PickVolume(float viewportX, float viewportY) const;

	private:
		/// @brief Appends a new volume of the given shape in front of the camera and selects it.
		void AddVolume(FogVolumeShape shape);

		/// @brief Computes where a new volume is placed: where the viewport centre's view ray hits
		///        collidable geometry, else the ground plane, else a point 10 units ahead.
		Vector3 ComputePlacementPosition() const;

		/// @brief Draws the property editor for one volume.
		void DrawVolumeProperties(FogVolume& volume);
	};
}
