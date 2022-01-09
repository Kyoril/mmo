// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "axis_display.h"

#include "manual_render_object.h"
#include "scene.h"

namespace mmo
{
	AxisDisplay::AxisDisplay(Scene& scene, const String& name)
		: m_scene(scene)
		, m_sceneNode(&m_scene.CreateSceneNode(name))
		, m_renderObject(m_scene.CreateManualRenderObject(name))
	{
		m_sceneNode->AttachObject(*m_renderObject);

		SetupManualRenderObject();
	}

	AxisDisplay::~AxisDisplay()
	{
		m_sceneNode->RemoveFromParent();
	}

	bool AxisDisplay::IsVisible() const noexcept
	{
		ASSERT(m_renderObject);
		return m_renderObject->IsVisible();
	}

	void AxisDisplay::SetVisible(const bool visible)
	{
		ASSERT(m_renderObject);
		m_renderObject->SetVisible(visible);
	}

	void AxisDisplay::SetupManualRenderObject() const
	{
		m_renderObject->Clear();

		const auto lineOperation = m_renderObject->AddLineListOperation();

		auto& xAxis = lineOperation->AddLine(Vector3::Zero, Vector3::UnitX);
		xAxis.SetColor(Color(1.0f, 0.0f, 0.0f));
		
		auto& yAxis = lineOperation->AddLine(Vector3::Zero, Vector3::UnitY);
		yAxis.SetColor(Color(0.0f, 1.0f, 0.0f));

		auto& zAxis = lineOperation->AddLine(Vector3::Zero, Vector3::UnitZ);
		zAxis.SetColor(Color(0.0f, 0.0f, 1.0f));
	}
}
