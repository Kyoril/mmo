// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "math/vector3.h"

#include <vector>

namespace mmo
{
	class Scene;
	class SceneNode;
	class ManualRenderObject;

	/// @brief What CollisionOverlay draws: baked collision faces plus faces a Cut shape removed.
	struct CollisionOverlayData
	{
		/// @brief Baked collision vertices.
		const std::vector<Vector3>* vertices { nullptr };
		/// @brief Baked collision triangle list.
		const std::vector<uint32>* indices { nullptr };
		/// @brief Per face: producing shape index or -1; may be null (everything render-derived).
		const std::vector<int32>* faceShape { nullptr };
		/// @brief Removed render faces, 3 vertices each.
		std::vector<Vector3> cutTriangles;
		/// @brief Shape whose faces are highlighted, or -1.
		int32 selectedShape { -1 };
		/// @brief Whether to draw triangle edges.
		bool wireframe { true };
	};

	/// @brief Semi-transparent collision visualisation coloured by walkability and origin.
	///        Reusable by any editor that has a Scene (mesh editor now, world model editor later).
	class CollisionOverlay final : public NonCopyable
	{
	public:
		/// @brief Creates the overlay's render objects under `parent`.
		CollisionOverlay(Scene& scene, SceneNode& parent);

		/// @brief Destroys the render objects.
		~CollisionOverlay() override;

	public:
		/// @brief Replaces everything drawn with `data`.
		void Rebuild(const CollisionOverlayData& data);

		/// @brief Shows or hides the overlay.
		void SetVisible(bool visible);

	private:
		Scene& m_scene;
		SceneNode* m_node { nullptr };
		ManualRenderObject* m_faces { nullptr };
	};
}
