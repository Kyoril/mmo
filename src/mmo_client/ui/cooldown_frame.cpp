// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "cooldown_frame.h"

#include "frame_ui/geometry_helper.h"
#include "graphics/graphics_device.h"
#include "scene_graph/material_manager.h"

namespace mmo
{
	CooldownFrame::CooldownFrame(const String& name)
		: Frame("Cooldown", name)
	{
		// We reset our buffer manually
		m_flags |= static_cast<uint32>(FrameFlags::ManualResetBuffer);

		// Create the vertex buffer for rendering
		m_hwBuffer = GraphicsDevice::Get().CreateVertexBuffer(6, sizeof(POS_COL_TEX_VERTEX), BufferUsage::DynamicWriteOnlyDiscardable, nullptr);

		// Register default properties and subscribe to their Changed events.
		m_propConnections += AddProperty("Material", "").Changed.connect(this, &CooldownFrame::OnMaterialChanged);
	}

	void CooldownFrame::SetProgress(const float progress)
	{
		if (std::fabs(progress - m_progress) < FLT_EPSILON)
		{
			return;
		}

		m_progress = std::clamp(progress, 0.0f, 1.0f);
		
		// Update the material parameter
		if (m_material)
		{
			m_material->SetScalarParameter("Percentage", m_progress);
		}
	}

	void CooldownFrame::SetMaterial(const String& materialPath)
	{
		auto* prop = GetProperty("Material");
		if (prop != nullptr)
		{
			prop->Set(materialPath);
		}
	}

	void CooldownFrame::Copy(Frame& other)
	{
		// Call base class Copy first
		Frame::Copy(other);

		
	}

	void CooldownFrame::DrawSelf()
	{
		// Don't render if progress is at 100% (no cooldown)
		if (m_progress >= 1.0f)
		{
			return;
		}

		// Don't render if we have no material
		if (!m_material)
		{
			return;
		}

		// Trigger recreation of geometry buffer if needed
		BufferGeometry();

		// Capture graphics state before applying UI material
		GraphicsDevice::Get().CaptureState();

		// Render using material instance
		m_material->Apply(GraphicsDevice::Get(), MaterialDomain::UserInterface, PixelShaderType::UI);

		// Bind additional constant buffers if any
		int psStartSlot = 2;
		if (const ConstantBufferPtr scalarBuffer = m_material->GetParameterBuffer(MaterialParameterType::Scalar, GraphicsDevice::Get()))
		{
			scalarBuffer->BindToStage(ShaderType::PixelShader, psStartSlot++);
		}
		if (const ConstantBufferPtr vectorBuffer = m_material->GetParameterBuffer(MaterialParameterType::Vector, GraphicsDevice::Get()))
		{
			vectorBuffer->BindToStage(ShaderType::PixelShader, psStartSlot++);
		}

		m_hwBuffer->Set(0);
		GraphicsDevice::Get().Draw(m_hwBuffer->GetVertexCount(), 0);

		// Restore graphics state after UI rendering
		GraphicsDevice::Get().RestoreState();
	}

	void CooldownFrame::PopulateGeometryBuffer()
	{
		if (!m_hwBuffer)
		{
			return;
		}

		const Rect frameRect = GetAbsoluteFrameRect();

		// Vertex buffer - simple quad covering the frame area
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

	void CooldownFrame::OnMaterialChanged(const Property& prop)
	{
		const String& materialPath = prop.GetValue();
		if (materialPath.empty())
		{
			m_material.reset();
			return;
		}

		// Load the material as a parent and create an instance
		MaterialPtr parentMaterial = MaterialManager::Get().Load(materialPath);
		if (parentMaterial)
		{
			m_material = std::make_shared<MaterialInstance>(GetName() + "_CooldownMat", parentMaterial);
			m_material->SetScalarParameter("Percentage", m_progress);
		}
	}
}
