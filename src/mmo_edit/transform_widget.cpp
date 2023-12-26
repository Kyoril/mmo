
#include "transform_widget.h"

#include <imgui.h>

#include "frame_ui/color.h"
#include "scene_graph/scene.h"
#include "scene_graph/camera.h"
#include "selectable.h"
#include "selection.h"
#include "frame_ui/mouse_event_args.h"
#include "log/default_log_levels.h"
#include "scene_graph/material_manager.h"

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

		CreatePlaneMesh();

		// Translation-Mode initialization
		SetupTranslation();

		// Rotation-Mode initialization
		SetupRotation();

		// Scale-Mode initialization
		SetupScale();

		m_widgetNode->SetVisible(false);

		// Watch for selection changes
		m_onSelectionChanged = m_selection.changed.connect(this, &TransformWidget::OnSelectionChanged);
	}

	TransformWidget::~TransformWidget()
	{
		if (m_rotationCenter)
		{
			m_scene.DestroySceneNode(*m_rotationCenter);
			m_rotationCenter = nullptr;
		}

		if (m_widgetNode)
		{
			m_widgetNode->RemoveAllChildren();
		}

		m_scene.DestroyManualRenderObject(*m_axisLines);
		if (m_xArrow) m_scene.DestroyEntity(*m_xArrow);
		if (m_yArrow) m_scene.DestroyEntity(*m_yArrow);
		if (m_zArrow) m_scene.DestroyEntity(*m_zArrow);
		if (m_xCircle) m_scene.DestroyEntity(*m_xCircle);
		if (m_yCircle) m_scene.DestroyEntity(*m_yCircle);
		if (m_zCircle) m_scene.DestroyEntity(*m_zCircle);
		if (m_fullCircleEntity) m_scene.DestroyEntity(*m_fullCircleEntity);
		m_scene.DestroyCamera(*m_dummyCamera);
		if (m_widgetNode)
		{
			m_scene.DestroySceneNode(*m_widgetNode);
			m_widgetNode = nullptr;
		}
	}

	void TransformWidget::Update(const Camera* camera)
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
		
		// Don't update the looks of the widget if nothing is selected or the user is flying around
		if (!m_selection.IsEmpty())
		{
			switch (m_mode)
			{
			case TransformMode::Translate:
				UpdateTranslation();
				break;
			case TransformMode::Rotate:
				UpdateRotation();
				break;
			case TransformMode::Scale:
				UpdateScale();
				break;
			}
		}

		// Update dummy camera
		m_dummyCamera->SetOrientation(m_camera.GetDerivedOrientation());
		m_dummyCamera->SetAspectRatio(m_camera.GetAspectRatio());
		m_relativeWidgetPos = m_widgetNode->GetPosition() - m_camera.GetDerivedPosition();
	}

	void TransformWidget::OnMouseMoved(const float x, const float y)
	{
		if (!m_selection.IsEmpty() &&
			!m_sleep &&
			m_visible)
		{
			if (m_copyMode && m_active)
			{
				m_copyMode = false;
				//ProjectManager.CopySelectedObjects();
			}

			Ray ray = m_dummyCamera->GetCameraToViewportRay(
				x,
				y,
				10000.0f);

			switch (m_mode)
			{
			case TransformMode::Translate:
				TranslationMouseMoved(ray, x, y);
				break;
			case TransformMode::Rotate:
				//RotationMouseMoved(ray, x, y);
				break;
			case TransformMode::Scale:
				//ScaleMouseMoved(ray, x, y);
				break;
			}
		}
	}

	void TransformWidget::OnMousePressed(uint32 buttons, float x, float y)
	{
		if (buttons == 0)
		{
			if (m_selectedAxis != axis_id::None && !m_sleep && m_visible)
			{
				m_active = true;

				switch (m_mode)
				{
				case TransformMode::Translate:
				{
					m_translation = Vector3::Zero;

					Plane plane = GetTranslatePlane(m_selectedAxis);
					Ray ray = m_dummyCamera->GetCameraToViewportRay(
						x,
						y,
						10000.0f);

					auto result = ray.Intersects(plane);
					if (result.first)
					{
						m_lastIntersection = ray.GetPoint(result.second);
					}

					break;
				}

				case TransformMode::Rotate:
					m_rotation = Quaternion::Identity;
					break;

				case TransformMode::Scale:
					//TODO
					break;
				}
			}
		}
	}

	void TransformWidget::OnMouseReleased(uint32 buttons, float x, float y)
	{
		if (buttons == 0)
		{
			m_active = false;

			switch (m_mode)
			{
			case TransformMode::Translate:
				FinishTranslation();
				break;

			case TransformMode::Rotate:
				FinishRotation();
				break;

			case TransformMode::Scale:
				FinishScale();
				break;
			}
		}
	}

	void TransformWidget::SetSnapToGrid(bool snap, float gridSize)
	{
		m_snap = snap;
		m_step = gridSize;
	}

	void TransformWidget::UpdateTanslationAxisLines()
	{
		const Vector3 xTipPos(CenterOffset + LineLength, 0.0f, 0.0f);
		const Vector3 yTipPos(0.0f, CenterOffset + LineLength, 0.0f);
		const Vector3 zTipPos(0.0f, 0.0f, CenterOffset + LineLength);

		m_axisLines->Clear();

		// X Axis
		{
			const Color color = (m_selectedAxis & axis_id::X) == 0 ? Color(1.0f, 0.0f, 0.0f, 1.0f) : Color(1.0f, 1.0f, 0.0f, 1.0f);

			const auto lineOp = m_axisLines->AddLineListOperation(MaterialManager::Get().Load("Models/Engine/Axis.hmat"));
			lineOp->AddLine(Vector3(CenterOffset, 0.0f, 0.0f), xTipPos).SetColor(color);
			lineOp->AddLine(Vector3(SquareLength, 0.0f, 0.0f), Vector3(SquareLength, SquareLength, 0.0f)).SetColor(color);
			lineOp->AddLine(Vector3(SquareLength, 0.0f, 0.0f), Vector3(SquareLength, 0.0f, SquareLength)).SetColor(color);
		}

		// Y Axis
		{
			const Color color = (m_selectedAxis & axis_id::Y) == 0 ? Color(0.0f, 1.0f, 0.0f, 1.0f) : Color(1.0f, 1.0f, 0.0f, 1.0f);

			const auto lineOp = m_axisLines->AddLineListOperation(MaterialManager::Get().Load("Models/Engine/Axis.hmat"));
			lineOp->AddLine(Vector3(0.0f, CenterOffset, 0.0f), yTipPos).SetColor(color);
			lineOp->AddLine(Vector3(0.0f, SquareLength, 0.0f), Vector3(SquareLength, SquareLength, 0.0f)).SetColor(color);
			lineOp->AddLine(Vector3(0.0f, SquareLength, 0.0f), Vector3(0.0f, SquareLength, SquareLength)).SetColor(color);
		}

		// Z Axis
		{
			const Color color = (m_selectedAxis & axis_id::Z) == 0 ? Color(0.0f, 0.0f, 1.0f, 1.0f) : Color(1.0f, 1.0f, 0.0f, 1.0f);

			const auto lineOp = m_axisLines->AddLineListOperation(MaterialManager::Get().Load("Models/Engine/Axis.hmat"));
			lineOp->AddLine(Vector3(0.0f, 0.0f, CenterOffset), zTipPos).SetColor(color);
			lineOp->AddLine(Vector3(0.0f, 0.0f, SquareLength), Vector3(0.0f, SquareLength, SquareLength)).SetColor(color);
			lineOp->AddLine(Vector3(0.0f, 0.0f, SquareLength), Vector3(SquareLength, 0.0f, SquareLength)).SetColor(color);
		}
	}

	void TransformWidget::SetupTranslation()
	{
		const Vector3 xTipPos(CenterOffset + LineLength, 0.0f, 0.0f);
		const Vector3 yTipPos(0.0f, CenterOffset + LineLength, 0.0f);
		const Vector3 zTipPos(0.0f, 0.0f, CenterOffset + LineLength);

		// Setup axis lines
		m_axisLines = m_scene.CreateManualRenderObject("__TransformAxisLines__");
		m_axisLines->SetRenderQueueGroupAndPriority(Overlay, 1000);
		m_axisLines->SetQueryFlags(0);

		UpdateTanslationAxisLines();

		// Create translation node
		m_translationNode = m_widgetNode->CreateChildSceneNode();
		m_translationNode->AttachObject(*m_axisLines);

		const MaterialPtr planeMaterial = MaterialManager::Get().Load("Models/Engine/AxisPlaneHighlight.hmat");

		// Setup arrows
		m_xArrowNode = m_translationNode->CreateChildSceneNode();
		m_xArrowNode->SetPosition(xTipPos);
		m_yArrowNode = m_translationNode->CreateChildSceneNode();
		m_yArrowNode->SetPosition(yTipPos);
		m_zArrowNode = m_translationNode->CreateChildSceneNode();
		m_zArrowNode->SetPosition(zTipPos);

		// X Arrow
		/*m_xArrow = m_sceneMgr.createEntity(ArrowMeshName);
		m_xArrow->setMaterialName("Editor/AxisX");
		m_xArrowNode->attachObject(m_xArrow);
		m_xArrowNode->yaw(Degree(90.0f));*/

		// Y Arrow
		/*m_yArrow = m_sceneMgr.createEntity(ArrowMeshName);
		m_yArrow->setMaterialName("Editor/AxisY");
		m_yArrowNode->attachObject(m_yArrow);
		m_yArrowNode->pitch(Degree(-90.0f));*/

		// Z Arrow
		/*m_zArrow = m_sceneMgr.createEntity(ArrowMeshName);
		m_zArrow->setMaterialName("Editor/AxisZ");
		m_zArrowNode->attachObject(m_zArrow);*/

		// Setup axis

		// Create on node for each plane and rotate
		m_xzPlaneNode = m_translationNode->CreateChildSceneNode();
		m_xyPlaneNode = m_translationNode->CreateChildSceneNode();
		m_xyPlaneNode->Pitch(Degree(-90.0f), TransformSpace::Local);
		m_yzPlaneNode = m_translationNode->CreateChildSceneNode();
		m_yzPlaneNode->Roll(Degree(90.0f), TransformSpace::Local);

		Entity* plane1 = m_scene.CreateEntity("AxisPlane1", m_translateAxisPlanes);
		plane1->SetRenderQueueGroupAndPriority(Overlay, 1000);
		plane1->SetMaterial(planeMaterial);
		plane1->SetQueryFlags(0);

		Entity* plane2 = m_scene.CreateEntity("AxisPlane2", m_translateAxisPlanes);
		plane2->SetRenderQueueGroupAndPriority(Overlay, 1000);
		plane2->SetMaterial(planeMaterial);
		plane2->SetQueryFlags(0);

		Entity* plane3 = m_scene.CreateEntity("AxisPlane3", m_translateAxisPlanes);
		plane3->SetRenderQueueGroupAndPriority(Overlay, 1000);
		plane3->SetMaterial(planeMaterial);
		plane3->SetQueryFlags(0);

		m_xzPlaneNode->AttachObject(*plane1);
		m_xyPlaneNode->AttachObject(*plane2);
		m_yzPlaneNode->AttachObject(*plane3);

		// Hide them initially
		m_xzPlaneNode->SetVisible(false);
		m_xyPlaneNode->SetVisible(false);
		m_yzPlaneNode->SetVisible(false);
	}

	void TransformWidget::SetupRotation()
	{
	}

	void TransformWidget::SetupScale()
	{
	}

	void TransformWidget::UpdateTranslation()
	{
		// Set x-axis color
		UpdateTanslationAxisLines();

		// Display xz-Plane
		if ((m_selectedAxis & axis_id::X) && (m_selectedAxis & axis_id::Z))
		{
			m_xzPlaneNode->SetVisible(true);
		}
		else
		{
			m_xzPlaneNode->SetVisible(false);
		}

		// Display xy-Plane
		if ((m_selectedAxis & axis_id::X) && (m_selectedAxis & axis_id::Y))
		{
			m_xyPlaneNode->SetVisible(true);
		}
		else
		{
			m_xyPlaneNode->SetVisible(false);
		}

		// Display yz-Plane
		if ((m_selectedAxis & axis_id::Y) && (m_selectedAxis & axis_id::Z))
		{
			m_yzPlaneNode->SetVisible(true);
		}
		else
		{
			m_yzPlaneNode->SetVisible(false);
		}
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
		if (!m_visible)
		{
			m_translationNode->SetVisible(false);
			//m_rotationNode->SetVisible(false);
			//m_scaleNode->SetVisible(false);
			return;
		}

		if (!m_selection.IsEmpty())
		{
			switch (m_mode)
			{
			case TransformMode::Translate:
				m_translationNode->SetVisible(true);
				m_xyPlaneNode->SetVisible(false);
				m_xzPlaneNode->SetVisible(false);
				m_yzPlaneNode->SetVisible(false);
				//m_rotationNode->SetVisible(false);
				//m_scaleNode->SetVisible(false);
				break;

			case TransformMode::Rotate:
				m_translationNode->SetVisible(false);
				//m_rotationNode->SetVisible(true);
				//m_scaleNode->SetVisible(false);
				break;

			case TransformMode::Scale:
				//m_scaleNode->SetVisible(true);
				//m_scaleXYPlaneNode->SetVisible(false);
				//m_scaleXZPlaneNode->SetVisible(false);
				//m_scaleYZPlaneNode->SetVisible(false);
				m_translationNode->SetVisible(false);
				//m_rotationNode->SetVisible(false);
				break;
			}
		}
	}

	void TransformWidget::ApplyTranslation(const Vector3& dir)
	{
		m_translation += dir;

		if (m_snap)
		{
			const Vector3 previousSnappedDir = m_snappedTranslation;
			m_snappedTranslation = Vector3(
				std::round(m_translation.x / m_step) * m_step,
				std::round(m_translation.y / m_step) * m_step,
				std::round(m_translation.z / m_step) * m_step);

			if (!m_snappedTranslation.IsNearlyEqual(previousSnappedDir))
			{
				const Vector3 diff = m_snappedTranslation - previousSnappedDir;
				for (auto& selected : m_selection.GetSelectedObjects())
				{
					selected->Translate(diff);
				}
			}
		}
		else
		{
			for (auto& selected : m_selection.GetSelectedObjects())
			{
				selected->Translate(dir);
			}
		}
	}

	void TransformWidget::FinishTranslation()
	{
		m_translation = Vector3::Zero;
		m_snappedTranslation = Vector3::Zero;
	}

	void TransformWidget::ApplyRotation(const Quaternion& rotation)
	{
	}

	void TransformWidget::FinishRotation()
	{
		if (m_rotationCenter == nullptr)
		{
			m_rotation = Quaternion::Identity;
		}
		else
		{
			for (auto& obj : m_selection.GetSelectedObjects())
			{
				/*obj.Position = obj.SceneNode._getDerivedPosition();
				obj.Orientation = obj.SceneNode._getDerivedOrientation();
				m_rotationCenter->removeChild(obj.SceneNode);
				m_sceneMgr.getRootSceneNode()->addChild(obj.SceneNode);*/
			}

			// Remove rotation center node
			m_scene.DestroySceneNode(*m_rotationCenter);
			m_rotationCenter = nullptr;
		}

		if (m_isLocal && !m_selection.IsEmpty())
		{
			const auto rot = m_selection.GetSelectedObjects().back()->GetOrientation();
			m_widgetNode->SetOrientation(rot);
		}
	}

	void TransformWidget::ApplyScale(const Vector3& dir)
	{
		for (auto& selected : m_selection.GetSelectedObjects())
		{
			selected->Scale(Vector3(dir.x, dir.y, dir.z));
		}

		m_scaleNode->Scale(dir);
	}

	void TransformWidget::FinishScale()
	{
		m_scaleNode->SetScale(Vector3(1.0f, 1.0f, 1.0f));
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
			return Plane{ m_widgetNode->GetOrientation() * Vector3::UnitY, m_relativeWidgetPos };
		}
		
		if (axis == (axis_id::X | axis_id::Y))
		{
			return Plane{ m_widgetNode->GetOrientation() * Vector3::UnitZ, m_relativeWidgetPos };
		}
		
		if (axis == (axis_id::Y | axis_id::Z))
		{
			return Plane{ m_widgetNode->GetOrientation() * Vector3::UnitX, m_relativeWidgetPos };
		}

		Vector3 camDir = (-m_relativeWidgetPos).NormalizedCopy();
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

		return Plane{ camDir, m_relativeWidgetPos };
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

	void TransformWidget::CreatePlaneMesh()
	{
		ManualRenderObject* planeObject = m_scene.CreateManualRenderObject("__AxisPlane__");
		planeObject->SetRenderQueueGroupAndPriority(Overlay, 1000);

		{
			const auto triangleOp = planeObject->AddTriangleListOperation(MaterialManager::Get().Load("Models/Engine/Axis.hmat"));
			triangleOp->AddTriangle(Vector3(SquareLength, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f), Vector3(SquareLength, 0.0f, SquareLength))
			          .SetColor(Color(1.0f, 1.0f, 0.0f, 0.6f));
			triangleOp->AddTriangle(
				Vector3(0.0f, 0.0f, 0.0f),
				Vector3(0.0f, 0.0f, SquareLength), 
				Vector3(SquareLength, 0.0f, SquareLength))
				.SetColor(Color(1.0f, 1.0f, 0.0f, 0.6f));
		}

		m_translateAxisPlanes = planeObject->ConvertToMesh("AxisPlane");
		m_scene.DestroyManualRenderObject(*planeObject);
	}

	void TransformWidget::TranslationMouseMoved(Ray& ray, const float x, const float y)
	{
		if (!m_active)
		{
			m_selectedAxis = axis_id::None;

			// Check for intersection between mouse and translation widget
			auto res = ray.Intersects(GetTranslatePlane(AxisId(axis_id::X | axis_id::Z)));
			if (res.first)
			{
				Vector3 dir = ray.GetPoint(res.second) - m_relativeWidgetPos;
				dir = m_widgetNode->GetOrientation().Inverse() * dir;
				if (dir.x > 0 && dir.x <= SquareLength * m_widgetNode->GetDerivedScale().x && dir.z > 0 && dir.z <= SquareLength * m_widgetNode->GetDerivedScale().x)
				{
					m_selectedAxis = AxisId(axis_id::X | axis_id::Z);
					return;
				}
			}

			if ((res = ray.Intersects(GetTranslatePlane(AxisId(axis_id::X | axis_id::Y)))).first)
			{
				Vector3 dir = ray.GetPoint(res.second) - m_relativeWidgetPos;
				dir = m_widgetNode->GetOrientation().Inverse() * dir;
				if (dir.x > 0 && dir.x <= SquareLength * m_widgetNode->GetDerivedScale().x && dir.y > 0 && dir.y <= SquareLength * m_widgetNode->GetDerivedScale().x)
				{
					m_selectedAxis = AxisId(axis_id::X | axis_id::Y);
					return;
				}
			}

			if ((res = ray.Intersects(GetTranslatePlane(AxisId(axis_id::Y | axis_id::Z)))).first)
			{
				Vector3 dir = ray.GetPoint(res.second) - m_relativeWidgetPos;
				dir = m_widgetNode->GetOrientation().Inverse() * dir;
				if (dir.z > 0 && dir.z <= SquareLength * m_widgetNode->GetDerivedScale().x && dir.y > 0 && dir.y <= SquareLength * m_widgetNode->GetDerivedScale().x)
				{
					m_selectedAxis = AxisId(axis_id::Y | axis_id::Z);
					return;
				}
			}

			if ((res = ray.Intersects(GetTranslatePlane(axis_id::X))).first)
			{
				Vector3 dir = ray.GetPoint(res.second) - m_relativeWidgetPos;
				dir = m_widgetNode->GetOrientation().Inverse() * dir;
								
				if (dir.x > (CenterOffset * m_widgetNode->GetDerivedScale().x) && dir.x <= (CenterOffset + LineLength + TipLength) * m_widgetNode->GetDerivedScale().x)
				{
					Vector3 projection = Vector3::UnitX * dir.Dot(Vector3::UnitX);
					Vector3 difference = dir - projection;
					if (difference.GetLength() < AxisBoxWidth * m_widgetNode->GetDerivedScale().x)
					{
						m_selectedAxis = axis_id::X;
						return;
					}
				}
			}
			if ((res = ray.Intersects(GetTranslatePlane(axis_id::Y))).first)
			{
				Vector3 dir = ray.GetPoint(res.second) - m_relativeWidgetPos;
				dir = m_widgetNode->GetOrientation().Inverse() * dir;
				if (dir.y > (CenterOffset * m_widgetNode->GetDerivedScale().x) && dir.y <= (CenterOffset + LineLength + TipLength) * m_widgetNode->GetDerivedScale().x)
				{
					Vector3 projection = Vector3::UnitY * dir.Dot(Vector3::UnitY);
					Vector3 difference = dir - projection;
					if (difference.GetLength() < AxisBoxWidth * m_widgetNode->GetDerivedScale().x)
					{
						m_selectedAxis = axis_id::Y;
						return;
					}
				}
			}
			if ((res = ray.Intersects(GetTranslatePlane(axis_id::Z))).first)
			{
				Vector3 dir = ray.GetPoint(res.second) - m_relativeWidgetPos;
				dir = m_widgetNode->GetOrientation().Inverse() * dir;
				if (dir.z > (CenterOffset * m_widgetNode->GetDerivedScale().x) && dir.z <= (CenterOffset + LineLength + TipLength) * m_widgetNode->GetDerivedScale().x)
				{
					Vector3 projection = Vector3::UnitZ * dir.Dot(Vector3::UnitZ);
					Vector3 difference = dir - projection;
					if (difference.GetLength() < AxisBoxWidth * m_widgetNode->GetDerivedScale().x)
					{
						m_selectedAxis = axis_id::Z;
						return;
					}
				}
			}
		}
		else
		{
			// Translate
			if (!m_selection.IsEmpty())
			{
				Vector3 distance(0.0f, 0.0f, 0.0f);

				Vector3 direction(
					((m_selectedAxis & axis_id::X) ? 1.0f : 0.0f),
					((m_selectedAxis & axis_id::Y) ? 1.0f : 0.0f),
					((m_selectedAxis & axis_id::Z) ? 1.0f : 0.0f));

				Plane plane = GetTranslatePlane(m_selectedAxis);
				auto res = ray.Intersects(plane);
				if (res.first)
				{
					Vector3 intersection = ray.GetPoint(res.second);
					distance = intersection - m_lastIntersection;
					distance = m_widgetNode->GetOrientation().Inverse() * distance;
					distance *= direction;
					distance = m_widgetNode->GetOrientation() * distance;
					m_lastIntersection = intersection;
				}

				ApplyTranslation(distance);
			}
		}
	}
}
