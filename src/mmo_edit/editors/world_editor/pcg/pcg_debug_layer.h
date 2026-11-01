// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "pcg/graph/debug_geometry.h"

#include <vector>

namespace mmo
{
	class IWorldEditor;
	class ManualRenderObject;
	class SceneNode;

	/// @brief Lines drawn into a world's viewport for a PCG graph's debug view: one wireframe render object
	///        ("Editor/Wireframe.hmat", no shadows, query flags 0) on its own scene node. Never saved and never
	///        part of the world; destroying the layer removes the lines.
	class PcgDebugLayer final : public NonCopyable
	{
	public:
		/// @brief Creates the (empty) render object and its scene node in the world.
		/// @param editor The world to draw into; must outlive the layer.
		/// @param ownerKey Identifies the owner (part of the render object's name).
		PcgDebugLayer(IWorldEditor& editor, uint64 ownerKey);

		/// @brief Destroys the render object and its scene node.
		~PcgDebugLayer() override;

	public:
		/// @brief Replaces the drawn lines.
		void SetLines(const std::vector<pcg::graph::DebugLine>& lines);

	private:
		IWorldEditor& m_editor;
		ManualRenderObject* m_object { nullptr };
		SceneNode* m_node { nullptr };
	};
}
