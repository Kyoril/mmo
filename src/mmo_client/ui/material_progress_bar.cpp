// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_progress_bar.h"

#include "graphics/graphics_device.h"
#include "scene_graph/material_manager.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace mmo
{
	namespace
	{
		/// Wraps the clock well before float precision suffers; materials use it only for slow sweeps.
		constexpr float materialTimeWrap = 3600.0f;

		Vector4 ToVector(const Color& color, const float opacity)
		{
			return { color.GetRed(), color.GetGreen(), color.GetBlue(), color.GetAlpha() * opacity };
		}
	}

	MaterialProgressBar::MaterialProgressBar(const String& name)
		: ProgressBar("ProgressBar", name)
	{
		m_materialConnections += AddProperty("FillMaterial", "").Changed.connect(this, &MaterialProgressBar::OnFillMaterialChanged);
		m_materialConnections += AddProperty("FillMaterialTint", "FFFFFFFF").Changed.connect(this, &MaterialProgressBar::OnFillMaterialTintChanged);
	}

	void MaterialProgressBar::Copy(Frame& other)
	{
		ProgressBar::Copy(other);
	}

	void MaterialProgressBar::Update(const float elapsed)
	{
		ProgressBar::Update(elapsed);

		if (m_material)
		{
			m_time += elapsed;
			if (m_time > materialTimeWrap)
			{
				m_time -= materialTimeWrap;
			}
		}
	}

	void MaterialProgressBar::PopulateGeometryBuffer()
	{
		if (!m_material)
		{
			ProgressBar::PopulateGeometryBuffer();
			return;
		}

		Frame::PopulateGeometryBuffer();
		GeometryBuffer& buffer = GetGeometryBuffer();

		RenderBackground();
		m_fillStartVertex = buffer.GetVertexCount();

		// The theme's own fill imagery, laid over the whole fill rect, gives the fill its shape (end caps,
		// rounded or pointed ends): the material draws these vertices and uses the texture's alpha as a
		// mask. Its vertex colours are replaced by the bar-space position (R = u, G = v), so the material
		// knows where along the bar each pixel lies however the imagery is sliced.
		const Rect rect = GetFillRect();
		m_aspect = rect.GetHeight() > 0.0f ? rect.GetWidth() / rect.GetHeight() : 1.0f;
		if (const auto* progressImagery = GetStateImageryByName("Progress"); progressImagery && rect.GetWidth() > 0.0f && rect.GetHeight() > 0.0f)
		{
			progressImagery->Render(rect, Color::White);
		}

		m_fillEndVertex = buffer.GetVertexCount();
		for (uint32 i = m_fillStartVertex; i < m_fillEndVertex; ++i)
		{
			const auto& vertex = buffer.GetVertex(i);
			const float u = std::clamp((vertex.pos.x - rect.left) / std::max(rect.GetWidth(), 1.0f), 0.0f, 1.0f);
			const float v = std::clamp((vertex.pos.y - rect.top) / std::max(rect.GetHeight(), 1.0f), 0.0f, 1.0f);
			buffer.SetVertexColor(i, Color(u, v, 0.0f, 1.0f).GetARGB());
		}

		RenderOverlay();

		// Without fill imagery, the material covers the plain fill rect (its mask parameter defaults to white).
		if (m_fillEndVertex == m_fillStartVertex)
		{
			if (!m_hwBuffer)
			{
				m_hwBuffer = GraphicsDevice::Get().CreateVertexBuffer(6, sizeof(POS_COL_TEX_VERTEX), BufferUsage::DynamicWriteOnlyDiscardable, nullptr);
			}

			const uint32 topLeft = Color(0.0f, 0.0f, 0.0f, 1.0f).GetARGB();
			const uint32 topRight = Color(1.0f, 0.0f, 0.0f, 1.0f).GetARGB();
			const uint32 bottomLeft = Color(0.0f, 1.0f, 0.0f, 1.0f).GetARGB();
			const uint32 bottomRight = Color(1.0f, 1.0f, 0.0f, 1.0f).GetARGB();
			const POS_COL_TEX_VERTEX vertices[] = {
				{ Vector3{ rect.left, rect.bottom, 0.0f }, bottomLeft, { 0.0f, 1.0f } },
				{ Vector3{ rect.left, rect.top, 0.0f }, topLeft, { 0.0f, 0.0f } },
				{ Vector3{ rect.right, rect.top, 0.0f }, topRight, { 1.0f, 0.0f } },

				{ Vector3{ rect.right, rect.top, 0.0f }, topRight, { 1.0f, 0.0f } },
				{ Vector3{ rect.right, rect.bottom, 0.0f }, bottomRight, { 1.0f, 1.0f } },
				{ Vector3{ rect.left, rect.bottom, 0.0f }, bottomLeft, { 0.0f, 1.0f } }
			};

			std::memcpy(m_hwBuffer->Map(LockOptions::Discard), vertices, sizeof(vertices));
			m_hwBuffer->Unmap();
		}
	}

	void MaterialProgressBar::DrawSelf()
	{
		if (!m_material)
		{
			ProgressBar::DrawSelf();
			return;
		}

		BufferGeometry();

		GeometryBuffer& buffer = GetGeometryBuffer();
		const uint32 vertexCount = buffer.GetVertexCount();
		const uint32 fillStart = std::min(m_fillStartVertex, vertexCount);
		const uint32 fillEnd = std::clamp(m_fillEndVertex, fillStart, vertexCount);

		buffer.DrawRange(0, fillStart);

		if ((m_displayProgress > 0.0f || m_trailProgress > 0.0f) && GetFillRect().GetWidth() > 0.0f)
		{
			DrawMaterialFill(fillStart, fillEnd - fillStart);
		}

		buffer.DrawRange(fillEnd, vertexCount - fillEnd);
	}

	void MaterialProgressBar::DrawMaterialFill(const uint32 firstVertex, const uint32 vertexCount)
	{
		const float opacity = GetOpacity(true);
		m_material->SetScalarParameter("Progress", std::clamp(m_displayProgress, 0.0f, 1.0f));
		m_material->SetScalarParameter("Trail", std::clamp(m_trailProgress, 0.0f, 1.0f));
		m_material->SetScalarParameter("Time", m_time);
		m_material->SetScalarParameter("Aspect", m_aspect);
		Color fillColor = m_progressColor;
		fillColor *= m_fillMaterialTint;
		m_material->SetVectorParameter("FillColor", ToVector(fillColor, opacity));
		m_material->SetVectorParameter("TrailColor", ToVector(m_trailColor, opacity));

		auto& gx = GraphicsDevice::Get();
		gx.CaptureState();

		m_material->Apply(gx, MaterialDomain::UserInterface, PixelShaderType::UI);

		int psStartSlot = 2;
		if (const ConstantBufferPtr scalarBuffer = m_material->GetParameterBuffer(MaterialParameterType::Scalar, gx))
		{
			scalarBuffer->BindToStage(ShaderType::PixelShader, psStartSlot++);
		}
		if (const ConstantBufferPtr vectorBuffer = m_material->GetParameterBuffer(MaterialParameterType::Vector, gx))
		{
			vectorBuffer->BindToStage(ShaderType::PixelShader, psStartSlot++);
		}

		if (vertexCount > 0)
		{
			// The fill imagery's texture replaces the material's "Mask" parameter (texture slot 0).
			GetGeometryBuffer().DrawRange(firstVertex, vertexCount, false);
		}
		else if (m_hwBuffer)
		{
			m_hwBuffer->Set(0);
			gx.Draw(m_hwBuffer->GetVertexCount(), 0);
		}

		gx.RestoreState();
	}
	void MaterialProgressBar::OnFillMaterialChanged(const Property& prop)
	{
		const String& materialPath = prop.GetValue();
		m_material.reset();

		if (!materialPath.empty())
		{
			if (const MaterialPtr parentMaterial = MaterialManager::Get().Load(materialPath))
			{
				m_material = std::make_shared<MaterialInstance>(GetName() + "_FillMat", parentMaterial);
			}
			else
			{
				WLOG("Progress bar " << GetName() << ": unable to load fill material " << materialPath);
			}
		}

		Invalidate(false);
	}

	void MaterialProgressBar::OnFillMaterialTintChanged(const Property& prop)
	{
		argb_t argb = 0xFFFFFFFF;
		std::stringstream stream(prop.GetValue());
		stream >> std::hex >> argb;
		m_fillMaterialTint = argb;
	}
}
