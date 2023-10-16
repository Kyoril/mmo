
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

		// Watch for selection changes
		m_onSelectionChanged = m_selection.changed.connect(this, &TransformWidget::OnSelectionChanged);

		// Setup material

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
		
		// Don't update the looks of the widget if nothing is selected or the user is flying around
		if (!m_selection.IsEmpty())
		{
			switch (m_mode)
			{
			case TransformMode::Translate:
			{
				UpdateTranslation();
				break;
			}

			case TransformMode::Rotate:
			{
				UpdateRotation();
				break;
			}

			case TransformMode::Scale:
			{
				UpdateScale();
				break;
			}
			}
		}

		// Update dummy camera
		m_dummyCamera->SetOrientation(m_camera.GetDerivedOrientation());
		m_dummyCamera->SetAspectRatio(m_camera.GetAspectRatio());
		m_relativeWidgetPos = m_widgetNode->GetPosition() - m_camera.GetDerivedPosition();
	}

	void TransformWidget::SetupTranslation()
	{
		// Setup axis lines
		m_axisLines = m_scene.CreateManualRenderObject("__TransformAxisLines__");
		m_axisLines->SetRenderQueueGroupAndPriority(Overlay, 1000);

		const Vector3 xTipPos(CenterOffset + LineLength, 0.0f, 0.0f);
		const Vector3 yTipPos(0.0f, CenterOffset + LineLength, 0.0f);
		const Vector3 zTipPos(0.0f, 0.0f, CenterOffset + LineLength);

		// X Axis
		{
			const auto lineOp = m_axisLines->AddLineListOperation();
			lineOp->AddLine(Vector3(CenterOffset, 0.0f, 0.0f), xTipPos).SetColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
			lineOp->AddLine(Vector3(SquareLength, 0.0f, 0.0f), Vector3(SquareLength, SquareLength, 0.0f)).SetColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
			lineOp->AddLine(Vector3(SquareLength, 0.0f, 0.0f), Vector3(SquareLength, 0.0f, SquareLength)).SetColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
			lineOp->SetDepthEnabled(false);
		}

		// Y Axis
		{
			const auto lineOp = m_axisLines->AddLineListOperation();
			lineOp->AddLine(Vector3(0.0f, CenterOffset, 0.0f), yTipPos).SetColor(Color(0.0f, 1.0f, 0.0f, 1.0f));
			lineOp->AddLine(Vector3(0.0f, SquareLength, 0.0f), Vector3(SquareLength, SquareLength, 0.0f)).SetColor(Color(0.0f, 1.0f, 0.0f, 1.0f));
			lineOp->AddLine(Vector3(0.0f, SquareLength, 0.0f), Vector3(0.0f, SquareLength, SquareLength)).SetColor(Color(0.0f, 1.0f, 0.0f, 1.0f));
			lineOp->SetDepthEnabled(false);
		}

		// Z Axis
		{
			const auto lineOp = m_axisLines->AddLineListOperation();
			lineOp->AddLine(Vector3(0.0f, 0.0f, CenterOffset), zTipPos).SetColor(Color(0.0f, 0.0f, 1.0f, 1.0f));
			lineOp->AddLine(Vector3(0.0f, 0.0f, SquareLength), Vector3(0.0f, SquareLength, SquareLength)).SetColor(Color(0.0f, 0.0f, 1.0f, 1.0f));
			lineOp->AddLine(Vector3(0.0f, 0.0f, SquareLength), Vector3(SquareLength, 0.0f, SquareLength)).SetColor(Color(0.0f, 0.0f, 1.0f, 1.0f));
			lineOp->SetDepthEnabled(false);
		}

		// Create translation node
		m_translationNode = m_widgetNode->CreateChildSceneNode();
		m_translationNode->AttachObject(*m_axisLines);

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
		/*if (!Ogre::MeshManager::getSingleton().resourceExists(AxisPlaneName))
		{
			Ogre::ManualObject* planeObj = m_sceneMgr.createManualObject();
			const Ogre::ColourValue planeColour(1.0f, 1.0f, 0.0f, 0.3f);
			planeObj->begin("Editor/AxisPlane", Ogre::RenderOperation::OT_TRIANGLE_STRIP);
			planeObj->position(SquareLength, 0.0f, 0.0f);
			planeObj->colour(planeColour);
			planeObj->position(0.0f, 0.0f, 0.0f);
			planeObj->colour(planeColour);
			planeObj->position(SquareLength, 0.0f, SquareLength);
			planeObj->colour(planeColour);
			planeObj->position(0.0f, 0.0f, SquareLength);
			planeObj->colour(planeColour);
			planeObj->end();

			m_translateAxisPlanes = planeObj->convertToMesh(AxisPlaneName);
			m_sceneMgr.destroyManualObject(planeObj);
		}
		else
		{
			m_translateAxisPlanes = Ogre::MeshManager::getSingleton().getByName(AxisPlaneName);
		}*/

		// Create on node for each plane and rotate
		m_xzPlaneNode = m_translationNode->CreateChildSceneNode();
		m_xyPlaneNode = m_translationNode->CreateChildSceneNode();
		m_xyPlaneNode->Pitch(Degree(-90.0f), TransformSpace::Local);
		m_yzPlaneNode = m_translationNode->CreateChildSceneNode();
		m_yzPlaneNode->Roll(Degree(90.0f), TransformSpace::Local);

		/*
		Ogre::Entity* plane1 = m_sceneMgr.createEntity(AxisPlaneName);
		plane1->setQueryFlags(0);
		plane1->setRenderQueueGroup(Ogre::RENDER_QUEUE_OVERLAY - 1);
		plane1->setCastShadows(false);
		Ogre::Entity* plane2 = m_sceneMgr.createEntity(AxisPlaneName);
		plane2->setQueryFlags(0);
		plane2->setRenderQueueGroup(Ogre::RENDER_QUEUE_OVERLAY - 1);
		plane2->setCastShadows(false);
		Ogre::Entity* plane3 = m_sceneMgr.createEntity(AxisPlaneName);
		plane3->setQueryFlags(0);
		plane3->setRenderQueueGroup(Ogre::RENDER_QUEUE_OVERLAY - 1);
		plane3->setCastShadows(false);

		m_xzPlaneNode->attachObject(plane1);
		m_xyPlaneNode->attachObject(plane2);
		m_yzPlaneNode->attachObject(plane3);
		*/

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

		for (auto& selected : m_selection.GetSelectedObjects())
		{
			selected->Translate(dir);
		}
	}

	void TransformWidget::FinishTranslation()
	{
		m_translation = Vector3::Zero;
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
