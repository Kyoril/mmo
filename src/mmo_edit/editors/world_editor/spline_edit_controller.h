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

			/// @brief Minimum number of points the spline may be reduced to by deletion.
			uint32 minPoints = 2;

			/// @brief Whether to draw direction chevrons along linear segments.
			bool drawDirection = false;

			/// @brief Optional label drawn next to the first point.
			std::string label;
		};

		/// @brief Returns the height to draw a point at for a world x/z.
		using HeightFunction = std::function<float(float x, float z)>;

	public:
		/// @param camera Camera used to project world positions to the viewport.
		/// @param height Height used to lift handles and lines onto the terrain or water.
		SplineEditController(Camera& camera, HeightFunction height);

	public:
		/// @brief Draws all splines; the selected one with handles. Remembers the viewport for hit tests.
		/// @param styles One style per spline (same order as splines).
		void Draw(ImDrawList& drawList, const ImVec2& viewportMin, const ImVec2& viewportSize,
			const std::vector<const PathSpline*>& splines, const std::vector<Style>& styles);

		/// @brief Handles a click: selects a point (and starts dragging it), or with alt inserts a point into
		///	       the segment under the cursor (and starts dragging the new point).
		/// @param x Screen x of the mouse.
		/// @param y Screen y of the mouse.
		/// @param alt Whether the insert modifier is held.
		/// @param worldHit World position under the cursor (terrain raycast), required for inserting.
		/// @return True if the click was consumed.
		bool OnMouseDown(float x, float y, std::vector<PathSpline*>& splines, bool alt, const Vector3* worldHit = nullptr);

		/// @brief Drags the selected point.
		/// @param x World x the point is dragged to.
		/// @param y World z the point is dragged to.
		/// @return True if a point moved.
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

		/// @brief Selected spline index or -1.
		int32 GetSelectedSpline() const { return m_selectedSpline; }

		/// @brief Selected point index or -1.
		int32 GetSelectedPoint() const { return m_selectedPoint; }

		/// @brief Changes the selection. Does not validate against any spline.
		void Select(int32 spline, int32 point) { m_selectedSpline = spline; m_selectedPoint = point; }

		/// @brief Starts dragging the selected point.
		/// @param moved Whether the drag counts as a change already (e.g. for a freshly added point).
		void BeginDrag(bool moved) { m_dragging = true; m_dragMoved = moved; }

		/// @brief Whether a point is being dragged.
		bool IsDragging() const { return m_dragging; }

		/// @brief Whether the current drag changed anything.
		bool DragMoved() const { return m_dragMoved; }

		/// @brief Aborts a drag without changing the selection.
		void CancelDrag() { m_dragging = false; m_dragMoved = false; }

	private:
		bool ProjectToScreen(const Vector3& worldPosition, ImVec2& screenPosition) const;

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

		Matrix4 m_viewProj = Matrix4::Identity;
		ImVec2 m_viewportMin{};
		ImVec2 m_viewportSize{};
	};
}
