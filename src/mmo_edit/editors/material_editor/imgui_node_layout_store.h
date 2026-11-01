// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "material_graph/node_layout_store.h"

namespace mmo
{
	/// @brief Node layout store backed by the node editor which is current while it is used. The material
	///        editor installs one on each graph it shows, so node positions are saved and restored.
	class ImGuiNodeLayoutStore final : public INodeLayoutStore
	{
	public:
		/// @copydoc INodeLayoutStore::GetNodeBounds
		/// @note Returns false if there is no current node editor.
		bool GetNodeBounds(uint32 nodeId, float& x, float& y, float& w, float& h) const override;

		/// @copydoc INodeLayoutStore::SetNodeBounds
		/// @note Does nothing if there is no current node editor.
		void SetNodeBounds(uint32 nodeId, float x, float y, float w, float h) override;
	};
}
