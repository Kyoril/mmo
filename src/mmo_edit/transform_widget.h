#pragma once

#include "base/non_copyable.h"
#include "base/signal.h"
#include "base/typedefs.h"
#include "base/vector.h"
#include "scene_graph/mesh.h"

namespace mmo
{
	class Entity;
	class SceneNode;
	class ManualRenderObject;
	class Camera;
	class Scene;
	class Selection;
	class Selected;

	enum class TransformMode
	{
		Translate,
		Rotate,
		Scale
	};

	namespace axis_id
	{
		enum Type
		{
			None = 0,

			X = 1,
			Y = 2,
			Z = 4,

			XY = X | Y,
			XZ = X | Z,
			YZ = Y | Z,

			All = X | Y | Z
		};
	}

	class TransformWidget final : public NonCopyable
	{
	public:
		signal<void()> modeChanged;
		signal<void()> coordinateSystemChanged;

	public:
		explicit TransformWidget(Selection& selection, Scene& scene, Camera& camera);
		~TransformWidget() override;

	public:
		void Update(Camera* camera);

		[[nodiscard]] const bool IsActive() const noexcept { return m_active; }

		void SetTransformMode(const TransformMode mode) { m_mode = mode; SetVisibility(); }

		void SetVisible(const bool visible) { m_visible = visible; SetVisibility(); }

	private:
		void SetupTranslation();

		void SetupRotation();

		void SetupScale();

		void UpdateTranslation();

		void UpdateRotation();

		void UpdateScale();

		void ChangeMode();

		void SetVisibility();

		void ApplyTranslation(const Vector3& dir);

		void FinishTranslation();

		void ApplyRotation(const Quaternion& rotation);

		void FinishRotation();

		void ApplyScale(const Vector3& dir);

		void FinishScale();

		void OnSelectionChanged();

		void OnPositionChanged(const Selected& object);

		void OnRotationChanged(const Selected& object);

		void OnScaleChanged(const Selected& object);

		void CancelTransform();

	private:
		// Common
		static const float LineLength;
		static const float CenterOffset;
		static const float SquareLength;
		static const float AxisBoxWidth;
		static const float PlaneHeight;
		static const float TipLength;
		static const float OrthoScale;

		// Translation
		static const uint32 ColorXAxis;
		static const uint32 ColorYAxis;
		static const uint32 ColorZAxis;
		static const uint32 ColorSelectedAxis;
		static const String AxisPlaneName;
		static const String ArrowMeshName;

		// Rotation
		static const float OuterRadius;
		static const float InnerRadius;
		static const String CircleMeshName;
		static const String FullCircleMeshName;

		// Scale
		static const String ScaleAxisPlaneName;
		static const String ScaleContentPlaneName;

	private:
		TransformMode m_mode { TransformMode::Translate };
		bool m_isLocal { true };
		float m_step { 1.0f };
		axis_id::Type m_selectedAxis { axis_id::None };
		Selection &m_selection;
		Vector3 m_snappedTranslation{};
		Camera *m_dummyCamera { nullptr };
		Vector3 m_relativeWidgetPos{};
		SceneNode *m_widgetNode { nullptr };
		SceneNode *m_translationNode { nullptr };
		SceneNode *m_rotationNode { nullptr };
		SceneNode *m_scaleNode { nullptr };
		Scene &m_scene;
		Camera &m_camera;
		bool m_active { false };
		Vector<size_t, 2> m_lastMouse = Vector<size_t, 2>{ 0, 0 };
		Vector3 m_lastIntersection{};
		Vector3 m_translation{};
		Quaternion m_rotation{};
		float m_scale{1.0f};
		bool m_keyDown { false};
		bool m_resetMouse{false};
		bool m_copyMode{false};
		bool m_sleep{false};
		Vector3 m_camDir{};
		bool m_visible{true};
		scoped_connection m_objectMovedCon;

		// Translation-Mode variables
		ManualRenderObject *m_axisLines{nullptr};
		SceneNode *m_xArrowNode{nullptr};
		SceneNode *m_yArrowNode{nullptr};
		SceneNode *m_zArrowNode{nullptr};
		Entity *m_xArrow{nullptr};
		Entity *m_yArrow{nullptr};
		Entity *m_zArrow{nullptr};
		SceneNode *m_xzPlaneNode{nullptr};
		SceneNode *m_xyPlaneNode{nullptr};
		SceneNode *m_yzPlaneNode{nullptr};
		MeshPtr m_translateAxisPlanes;

		// Rotation-Mode variables
		SceneNode *m_zRotNode{nullptr};
		SceneNode *m_xRotNode{nullptr};
		SceneNode *m_yRotNode{nullptr};
		Entity *m_xCircle{nullptr};
		Entity *m_yCircle{nullptr};
		Entity *m_zCircle{nullptr};
		Entity *m_fullCircleEntity{nullptr};
		SceneNode *m_rotationCenter{nullptr};
		MeshPtr m_circleMesh;
		MeshPtr m_fullCircleMesh;

		// Scale-Mode variables
		ManualRenderObject *m_scaleAxisLines{nullptr};
		SceneNode *m_scaleXZPlaneNode{nullptr};
		SceneNode *m_scaleXYPlaneNode{nullptr};
		SceneNode *m_scaleYZPlaneNode{nullptr};
		SceneNode *m_scaleContentPlaneNode{nullptr};
		MeshPtr m_scaleAxisPlanes;
		MeshPtr m_scaleCenterPlanes;
	};
}
