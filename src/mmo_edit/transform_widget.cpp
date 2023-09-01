
#include "transform_widget.h"

#include "frame_ui/color.h"
#include "scene_graph/scene.h"
#include "scene_graph/camera.h"
#include "selectable.h"
#include "selection.h"

namespace mmo
{
	constexpr float TransformWidget::LineLength = 1.0f;
	constexpr float TransformWidget::CenterOffset = 0.3f;
	constexpr float TransformWidget::SquareLength = 0.5f;
	constexpr float TransformWidget::AxisBoxWidth = 0.1f;
	constexpr float TransformWidget::PlaneHeight = 0.1f;
	constexpr float TransformWidget::TipLength = 0.3f;
	constexpr float TransformWidget::OrthoScale = 150.0f;
	const uint32 TransformWidget::ColorXAxis = Color(1.0f, 0.0f, 0.0f);
	const uint32 TransformWidget::ColorYAxis = Color(0.0f, 1.0f, 0.0f);
	const uint32 TransformWidget::ColorZAxis = Color(0.0f, 0.0f, 1.0f);
	const uint32 TransformWidget::ColorSelectedAxis = Color(1.0f, 1.0f, 0.0f);
	const String TransformWidget::AxisPlaneName = "Editor/AxisPlane";
	const String TransformWidget::ArrowMeshName = "Arrow.mesh";
	constexpr float TransformWidget::OuterRadius = 1.0f;
	constexpr float TransformWidget::InnerRadius = 0.8f;
	const String TransformWidget::CircleMeshName = "Editor/RotationCircle";
	const String TransformWidget::FullCircleMeshName = "Editor/FullRotationCircle";
	const String TransformWidget::ScaleAxisPlaneName = "Editor/ScaleAxisPlane";
	const String TransformWidget::ScaleContentPlaneName = "Editor/ScaleContentPlane";

	TransformWidget::TransformWidget(Selection& selection, Scene& scene, Camera& camera)
		: m_selection(selection)
		, m_scene(scene)
		, m_camera(camera)
	{
		m_widgetNode = m_scene.GetRootSceneNode().CreateChildSceneNode();

		// Create and setup dummy cam
		m_dummyCamera = m_scene.CreateCamera("DummyCam-" + m_widgetNode->GetName());
		m_dummyCamera->SetAspectRatio(camera.GetAspectRatio());

		// Translation-Mode initialization
		SetupTranslation();

		// Rotation-Mode initialization
		SetupRotation();

		// Scale-Mode initialization
		SetupScale();

		m_widgetNode->SetVisible(false);
	}

	TransformWidget::~TransformWidget()
	{
	}

	void TransformWidget::Update(Camera* camera)
	{
		if (!m_active)
		{
			// Scale with camera distance
			const Vector3 distance = m_widgetNode->GetPosition() - camera->GetDerivedPosition();
			m_scale = distance.GetLength() * 0.15f;
			m_widgetNode->SetScale(Vector3(m_scale, m_scale, m_scale));
		}

		// Flip the rotation widget depending on the camera position
		switch (m_mode)
		{
			case TransformMode::Rotate:
			{
				m_camDir = m_camera.GetDerivedPosition() - m_widgetNode->GetPosition();
				m_camDir = m_widgetNode->GetOrientation().Inverse() * m_camDir;

				Vector3 xScale(1.0f, 1.0f, 1.0f);
				Vector3 yScale(1.0f, 1.0f, 1.0f);
				Vector3 zScale(1.0f, 1.0f, 1.0f);

				if (m_camDir.y < 0.0f)
				{
					xScale.y = -1.0f;
					zScale.y = -1.0f;
				}

				if (m_camDir.x < 0.0f)
				{
					yScale.x = -1.0f;
					zScale.x = -1.0f;
				}

				if (m_camDir.z < 0.0f)
				{
					xScale.x = -1.0f;
					yScale.y = -1.0f;
				}

				m_xRotNode->SetScale(xScale);
				m_yRotNode->SetScale(yScale);
				m_zRotNode->SetScale(zScale);
			} break;

			case TransformMode::Scale:
			{
				m_camDir = m_camera.GetDerivedPosition() - m_widgetNode->GetPosition();
				m_camDir = m_widgetNode->GetOrientation().Inverse() * m_camDir;

				Vector3 scale(1.0f, 1.0f, 1.0f);
				if (m_camDir.y < 0.0f)
				{
					scale.y = -1.0f;
				}
				if (m_camDir.x < 0.0f)
				{
					scale.x = -1.0f;
				}
				if (m_camDir.z < 0.0f)
				{
					scale.z = -1.0f;
				}

				m_scaleNode->SetScale(scale);
			} break;
		}
				
		m_relativeWidgetPos = m_widgetNode->GetPosition() - m_camera.GetDerivedPosition();
	}

	void TransformWidget::SetupTranslation()
	{

	}

	void TransformWidget::SetupRotation()
	{
	}

	void TransformWidget::SetupScale()
	{
	}

	void TransformWidget::UpdateTranslation()
	{
	}

	void TransformWidget::UpdateRotation()
	{
	}

	void TransformWidget::UpdateScale()
	{
	}

	void TransformWidget::ChangeMode()
	{
	}

	void TransformWidget::SetVisibility()
	{
	}

	void TransformWidget::ApplyTranslation(const Vector3& dir)
	{
	}

	void TransformWidget::FinishTranslation()
	{
	}

	void TransformWidget::ApplyRotation(const Quaternion& rotation)
	{
	}

	void TransformWidget::FinishRotation()
	{
	}

	void TransformWidget::ApplyScale(const Vector3& dir)
	{
	}

	void TransformWidget::FinishScale()
	{
	}

	void TransformWidget::OnSelectionChanged()
	{
		if (!m_selection.IsEmpty())
		{
			// Connect
			m_objectMovedCon = m_selection.GetSelectedObjects().back()->positionChanged.connect(this, &TransformWidget::OnPositionChanged);

			// Get position
			const auto& pos = m_selection.GetSelectedObjects().back()->GetPosition();
			m_widgetNode->SetPosition(pos);
			m_relativeWidgetPos = m_widgetNode->GetPosition() - m_camera.GetDerivedPosition();

			if (m_isLocal)
			{
				const auto rot = m_selection.GetSelectedObjects().back()->GetOrientation();
				m_widgetNode->SetOrientation(rot);
			}
			else
			{
				m_widgetNode->SetOrientation(Quaternion::Identity);
			}

			SetVisibility();
		}
		else
		{
			if (m_objectMovedCon.connected())
			{
				m_objectMovedCon.disconnect();
			}

			m_widgetNode->SetVisible(false);
			m_selectedAxis = axis_id::None;
		}
	}

	void TransformWidget::OnPositionChanged(const Selectable& object)
	{
		const auto& pos = object.GetPosition();

		m_widgetNode->SetPosition(pos);
		m_relativeWidgetPos = pos - m_camera.GetDerivedPosition();
	}

	void TransformWidget::OnRotationChanged(const Selectable& object)
	{
	}

	void TransformWidget::OnScaleChanged(const Selectable& object)
	{
	}

	void TransformWidget::CancelTransform()
	{
	}

	Plane TransformWidget::GetTranslatePlane(AxisId axis)
	{
		if (axis == (axis_id::X | axis_id::Z))
		{
			return Plane(m_widgetNode->GetOrientation() * Vector3::UnitY, m_relativeWidgetPos);
		}
		
		if (axis == (axis_id::X | axis_id::Y))
		{
			return Plane(m_widgetNode->GetOrientation() * Vector3::UnitZ, m_relativeWidgetPos);
		}
		
		if (axis == (axis_id::Y | axis_id::Z))
		{
			return Plane(m_widgetNode->GetOrientation() * Vector3::UnitX, m_relativeWidgetPos);
		}

		Vector3 camDir = -m_relativeWidgetPos;
		camDir = m_widgetNode->GetOrientation().Inverse() * camDir;

		switch (axis)
		{
		case axis_id::X:
		{
			camDir.x = 0.0f;
			break;
		}

		case axis_id::Y:
		{
			camDir.y = 0.0f;
			break;
		}

		case axis_id::Z:
		{
			camDir.z = 0.0f;
			break;
		}
		}

		camDir = m_widgetNode->GetOrientation() * camDir;
		camDir.Normalize();

		return Plane(camDir, m_relativeWidgetPos);
	}

	Plane TransformWidget::GetScalePlane(AxisId axis)
	{
		if (axis == (axis_id::X | axis_id::Z))
		{
			return Plane(m_widgetNode->GetOrientation() * Vector3::UnitY, m_relativeWidgetPos);
		}
		
		if (axis == (axis_id::X | axis_id::Y))
		{
			return Plane(m_widgetNode->GetOrientation() * Vector3::UnitZ, m_relativeWidgetPos);
		}
		
		if (axis == (axis_id::Y | axis_id::Z))
		{
			return Plane(m_widgetNode->GetOrientation() * Vector3::UnitX, m_relativeWidgetPos);
		}

		Vector3 camDir = -m_relativeWidgetPos;
		camDir = m_widgetNode->GetOrientation().Inverse() * camDir;

		switch (axis)
		{
		case axis_id::X:
		{
			camDir.x = 0.0f;
			break;
		}

		case axis_id::Y:
		{
			camDir.y = 0.0f;
			break;
		}

		case axis_id::Z:
		{
			camDir.z = 0.0f;
			break;
		}
		}

		camDir = m_widgetNode->GetOrientation() * camDir;
		camDir.Normalize();

		return Plane(camDir, m_relativeWidgetPos);
	}
}
