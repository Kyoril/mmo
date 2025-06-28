
#include "minimap_frame.h"

#include "minimap.h"
#include "frame_ui/geometry_helper.h"
#include "graphics/material_instance.h"
#include "scene_graph/material_manager.h"

namespace mmo
{
	MinimapFrame::MinimapFrame(const String& name, Minimap& minimap)
		: Frame("Minimap", name)
		, m_minimap(minimap)
	{
		// We reset our buffer manually (actually we don't even use it!)
		m_flags |= static_cast<uint32>(FrameFlags::ManualResetBuffer);

		// Ensure the material instance is created so we can use it to inject the actual minimap texture into it
		m_material = std::make_shared<MaterialInstance>("MinimapMaterialInstance",
			MaterialManager::Get().Load("Interface/MinimapFrame.hmat"));
		if (m_material)
		{
			// Bind the minimap texture to this material instance
			m_material->SetTextureParameter("Minimap", m_minimap.GetMinimapTexture());
		}

		m_hwBuffer = GraphicsDevice::Get().CreateVertexBuffer(6, sizeof(POS_COL_TEX_VERTEX), BufferUsage::DynamicWriteOnlyDiscardable, nullptr);
	}

	void MinimapFrame::DrawSelf()
	{
		if (IsVisible(false))
		{
			m_minimap.RenderMinimap();
		}

		// Trigger recreation of geometry buffer if needed
		BufferGeometry();

		// Capture graphics state before applying UI material
		GraphicsDevice::Get().CaptureState();

		// Render minimap using material instance
		m_material->Apply(GraphicsDevice::Get(), MaterialDomain::UserInterface, PixelShaderType::UI);

		m_hwBuffer->Set(0);
		GraphicsDevice::Get().Draw(m_hwBuffer->GetVertexCount(), 0);

		// Restore graphics state after UI rendering
		GraphicsDevice::Get().RestoreState();
	}

	void MinimapFrame::PopulateGeometryBuffer()
	{
		// First, we request the current hw buffer capacity
		size_t size = m_hwBuffer->GetVertexCount();

		const Rect frameRect = GetAbsoluteFrameRect();

		// Vertex buffer
		const POS_COL_TEX_VERTEX vertices[] = {
			{ Vector3{frameRect.left, frameRect.bottom, 0.0f }, Color::White, { 0.0f, 1.0f }},
			{ Vector3{frameRect.left, frameRect.top, 0.0f }, Color::White, { 0.0f, 0.0f }},
			{ Vector3{frameRect.right, frameRect.top, 0.0f }, Color::White, { 1.0f, 0.0f }},

			{ Vector3{frameRect.right, frameRect.top, 0.0f }, Color::White, { 1.0f, 0.0f }},
			{ Vector3{frameRect.right, frameRect.bottom, 0.0f }, Color::White, { 1.0f, 1.0f }},
			{ Vector3{frameRect.left, frameRect.bottom, 0.0f }, Color::White, { 0.0f, 1.0f }}
		};

		// Update buffer
		std::memcpy(m_hwBuffer->Map(LockOptions::Discard), vertices, sizeof(POS_COL_TEX_VERTEX) * std::size(vertices));
		m_hwBuffer->Unmap();
	}
}
