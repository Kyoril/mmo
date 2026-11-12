// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "pcg_debug_layer.h"

#include "editors/world_editor/edit_modes/world_edit_mode.h"

#include "scene_graph/manual_render_object.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/scene_node.h"

namespace mmo
{
	PcgDebugLayer::PcgDebugLayer(IWorldEditor& editor, const uint64 ownerKey)
		: m_editor(editor)
	{
		m_object = m_editor.CreateManualRenderObject("__PcgDebug_" + std::to_string(ownerKey));
		if (m_object != nullptr)
		{
			m_object->SetCastShadows(false);
			m_object->SetQueryFlags(0);
		}

		m_node = m_editor.CreateChildSceneNode();
		if (m_node != nullptr && m_object != nullptr)
		{
			m_node->AttachObject(*m_object);
		}
	}

	PcgDebugLayer::~PcgDebugLayer()
	{
		if (m_object != nullptr)
		{
			m_editor.DestroyManualRenderObject(*m_object);
			m_object = nullptr;
		}
		if (m_node != nullptr)
		{
			m_editor.DestroySceneNode(*m_node);
			m_node = nullptr;
		}
	}

	void PcgDebugLayer::SetLines(const std::vector<pcg::graph::DebugLine>& lines)
	{
		if (m_object == nullptr)
		{
			return;
		}

		m_object->Clear();
		if (lines.empty())
		{
			return;
		}

		const MaterialPtr material = MaterialManager::Get().Load("Editor/Wireframe.hmat");
		if (!material)
		{
			return;
		}

		{
			// The operation uploads its lines when the reference goes out of scope.
			auto operation = m_object->AddLineListOperation(material);
			for (const pcg::graph::DebugLine& line : lines)
			{
				operation->AddLine(line.a, line.b).SetColor(line.argb);
			}
		}

		// The bounds changed: let the scene re-place the node (like the foliage brush does).
		if (m_node != nullptr)
		{
			m_node->SetPosition(Vector3::Zero);
		}
	}
}
