// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/matrix4.h"
#include "math/path_spline.h"
#include "math/vector3.h"

#include <imgui.h>

#include <functional>
#include <string>
#include <vector>

namespace mmo
{
	class Camera;

	/// @brief Viewport editing of PathSpline control points: drawing, picking, dragging, inserting and
	///	       deleting points. Owns the selection (spline + point index) but not the splines themselves;
	///	       callers pass them in on every call, so any container can back them.
	class SplineEditController final
	{
	public:
		/// @brief How one spline is drawn.
		struct Style
		{
			/// @brief Colour of the spline's line and unselected handles.
			uint32 lineColor = 0xFFFFFFFF;

			/// @brief Colour of the selected point's handle fill.
			uint32 pointColor = 0xFFFFFFFF;

			/// @brief Colour of the line of the selected spline.
			uint32 selectedColor = 0xFFFFFFFF;

			/// @brief Whether to draw the width ribbon (both sides offset by the point width).
			bool drawWidthRibbon = false;

			/// @brief Whether to draw direction chevrons along linear segments.
			bool drawDirection = false;

			/// @brief Optional label drawn next to the first point.
			std::string label;
		};

		/// @brief Returns the height to draw a point at for a world x/z.
		using HeightFunction = std::function<float(float x, float z)>;

		/// @brief A tangent handle of the selected point (curved splines only).
		enum class TangentHandle : uint8
		{
			/// @brief No handle.
			None,

			/// @brief The arriving handle.
			In,

			/// @brief The leaving handle.
			Out
		};

	public:
		/// @param camera Camera used to project world positions to the viewport.
		/// @param height Height used to lift handles and lines onto the terrain or water.
		SplineEditController(Camera& camera, HeightFunction height);

	public:
		/// @brief Draws all splines; the selected one with handles, and the tangent handles of the selected
		///	       point of a curved spline (in and out in distinct colours; filled = manual, hollow = auto).
		///	       Remembers the viewport for hit tests.
		/// @param styles One style per spline (same order as splines).
		void Draw(ImDrawList& drawList, const ImVec2& viewportMin, const ImVec2& viewportSize,
			const std::vector<const PathSpline*>& splines, const std::vector<Style>& styles);

		/// @brief Handles a click: grabs a tangent handle of the selected point (curved splines), selects a
		///	       point (and starts dragging it), or with alt inserts a point into the segment under the
		///	       cursor (and starts dragging the new point). Handles are tested before points, except with alt.
		/// @param x Screen x of the mouse.
		/// @param y Screen y of the mouse.
		/// @param alt Whether the insert modifier is held.
		/// @param worldHit World position under the cursor (terrain raycast). Linear splines need it to insert;
		///	       curved (3D) ones insert on the drawn curve nearest the cursor and use it only as a fallback.
		/// @return True if the click was consumed.
		bool OnMouseDown(float x, float y, std::vector<PathSpline*>& splines, bool alt, const Vector3* worldHit = nullptr);

		/// @brief Drags the selected point, or the grabbed tangent handle. The first move of a handle makes
		///	       the point's tangents manual (keeping the other handle where it is); only the dragged handle
		///	       changes. Only x/z change; the point's or handle's height stays.
		/// @param x World x the point or handle is dragged to.
		/// @param y World z the point or handle is dragged to.
		/// @return True if anything moved.
		bool OnMouseMoved(float x, float y, std::vector<PathSpline*>& splines);

		/// @brief Ends a drag.
		void OnMouseUp();

		/// @brief Deletes the selected point unless the spline would drop below minPoints.
		/// @return True if a point was deleted.
		bool DeleteSelectedPoint(std::vector<PathSpline*>& splines, uint32 minPoints);

		/// @brief Finds the control point under the screen position.
		bool HitTestPoint(float x, float y, const std::vector<PathSpline*>& splines, int32& splineIndex, int32& pointIndex) const;

		/// @brief Finds the segment under the screen position (reports its first point).
		bool HitTestSegment(float x, float y, const std::vector<PathSpline*>& splines, int32& splineIndex, int32& pointIndex) const;

		/// @brief Finds the tangent handle of the selected point under the screen position. Only curved
		///	       splines have handles; a handle lying on its point is left to the point.
		/// @param handle Receives the handle when the call returns true.
		bool HitTestTangentHandle(float x, float y, const std::vector<PathSpline*>& splines, TangentHandle& handle) const;

		/// @brief Gets the world position of what is being dragged (the point or the grabbed handle). Drags
		///	       move on the horizontal plane through this height.
		/// @return False when nothing valid is being dragged.
		bool GetDragAnchor(const std::vector<PathSpline*>& splines, float& x, float& y, float& z) const;

		/// @brief The tangent handle being dragged, or None while dragging a point (or not dragging).
		TangentHandle GetDraggedHandle() const { return m_dragHandle; }

		/// @brief Whether a spline shows and uses tangent handles (curved splines only).
		static bool HasTangents(const PathSpline& spline) { return spline.interpolation != SplineInterpolation::Linear; }

		/// @brief Switches one point back to automatic tangents.
		/// @return True if the point had manual tangents.
		static bool ResetTangents(PathSpline& spline, size_t pointIndex);

		/// @brief Switches every point of the spline back to automatic tangents.
		/// @return True if any point had manual tangents.
		static bool ResetAllTangents(PathSpline& spline);

		/// @brief Selected spline index or -1.
		int32 GetSelectedSpline() const { return m_selectedSpline; }

		/// @brief Selected point index or -1.
		int32 GetSelectedPoint() const { return m_selectedPoint; }

		/// @brief Changes the selection. Does not validate against any spline.
		void Select(int32 spline, int32 point) { m_selectedSpline = spline; m_selectedPoint = point; }

		/// @brief Starts dragging the selected point.
		/// @param moved Whether the drag counts as a change already (e.g. for a freshly added point).
		void BeginDrag(bool moved) { m_dragging = true; m_dragMoved = moved; m_dragHandle = TangentHandle::None; }

		/// @brief Starts dragging a tangent handle of the selected point.
		void BeginHandleDrag(TangentHandle handle) { m_dragging = true; m_dragMoved = false; m_dragHandle = handle; }

		/// @brief Whether a point is being dragged.
		bool IsDragging() const { return m_dragging; }

		/// @brief Whether the current drag changed anything.
		bool DragMoved() const { return m_dragMoved; }

		/// @brief Aborts a drag without changing the selection.
		void CancelDrag() { m_dragging = false; m_dragMoved = false; m_dragHandle = TangentHandle::None; }

	private:
		bool ProjectToScreen(const Vector3& worldPosition, ImVec2& screenPosition) const;

		/// @brief The selected point of a curved selected spline, or null.
		const SplinePoint* GetSelectedCurvePoint(const std::vector<PathSpline*>& splines) const;

		/// @brief World position a point is shown at: its own height on curved (3D) splines, the height
		///	       function's on linear ones.
		Vector3 GetPointPosition(const PathSpline& spline, const SplinePoint& point) const;

		/// @brief Projects a control point at GetPointPosition.
		bool ProjectSplinePoint(const PathSpline& spline, const SplinePoint& point, ImVec2& screenPosition) const;

		/// @brief Projects a 3D tangent handle of a curved spline's point (point + offset).
		bool ProjectHandle(const SplinePoint& point, float offsetX, float offsetY, float offsetZ, ImVec2& screenPosition) const;

		/// @brief Projects a point of the x/z plane, lifted by the height function.
		bool ProjectPlanePoint(float x, float z, ImVec2& screenPosition) const;

		/// @brief Number of segments a spline has (the closing one included).
		static size_t GetSegmentCount(const PathSpline& spline);

	private:
		Camera& m_camera;
		HeightFunction m_height;

		int32 m_selectedSpline = -1;
		int32 m_selectedPoint = -1;
		bool m_dragging = false;
		bool m_dragMoved = false;
		TangentHandle m_dragHandle = TangentHandle::None;

		Matrix4 m_viewProj = Matrix4::Identity;
		ImVec2 m_viewportMin{};
		ImVec2 m_viewportSize{};
	};
}
