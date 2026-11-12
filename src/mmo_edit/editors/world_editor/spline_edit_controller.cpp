// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spline_edit_controller.h"

#include "math/vector4.h"
#include "scene_graph/camera.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mmo
{
	namespace
	{
		/// Pick radius of a control point in screen pixels.
		constexpr float PointPickRadius = 9.0f;

		/// Maximum polyline step when drawing curved splines, in world units.
		constexpr float CurveDrawStep = 4.0f;

		/// Screen radius of a tangent handle.
		constexpr float HandleRadius = 5.0f;

		/// Arriving handle colour (orange).
		constexpr ImU32 InHandleColor = IM_COL32(255, 150, 40, 255);

		/// Leaving handle colour (green).
		constexpr ImU32 OutHandleColor = IM_COL32(90, 230, 90, 255);

		float DistanceToSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b)
		{
			const float dx = b.x - a.x;
			const float dy = b.y - a.y;
			const float lengthSq = dx * dx + dy * dy;
			const float t = lengthSq > 0.0f ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSq, 0.0f, 1.0f) : 0.0f;
			return std::hypot(p.x - (a.x + dx * t), p.y - (a.y + dy * t));
		}
	}

	SplineEditController::SplineEditController(Camera& camera, HeightFunction height)
		: m_camera(camera)
		, m_height(std::move(height))
	{
	}

	size_t SplineEditController::GetSegmentCount(const PathSpline& spline)
	{
		const size_t count = spline.points.size();
		if (count < 2)
		{
			return 0;
		}
		return spline.closed ? count : count - 1;
	}

	bool SplineEditController::ProjectToScreen(const Vector3& worldPosition, ImVec2& screenPosition) const
	{
		const Vector4 clip = m_viewProj * Vector4(worldPosition, 1.0f);
		if (clip.w <= 0.0f)
		{
			return false;
		}

		screenPosition.x = m_viewportMin.x + (clip.x / clip.w * 0.5f + 0.5f) * m_viewportSize.x;
		screenPosition.y = m_viewportMin.y + (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * m_viewportSize.y;
		return true;
	}

	bool SplineEditController::ProjectPlanePoint(const float x, const float z, ImVec2& screenPosition) const
	{
		return ProjectToScreen(Vector3(x, m_height ? m_height(x, z) : 0.0f, z), screenPosition);
	}

	Vector3 SplineEditController::GetPointPosition(const PathSpline& spline, const SplinePoint& point) const
	{
		// Curved splines are 3D and keep their own heights; linear ones (rivers) lie on the surface.
		if (HasTangents(spline))
		{
			return Vector3(point.x, point.y, point.z);
		}
		return Vector3(point.x, m_height ? m_height(point.x, point.z) : 0.0f, point.z);
	}

	bool SplineEditController::ProjectSplinePoint(const PathSpline& spline, const SplinePoint& point, ImVec2& screenPosition) const
	{
		return ProjectToScreen(GetPointPosition(spline, point), screenPosition);
	}

	bool SplineEditController::ProjectHandle(const SplinePoint& point, const float offsetX, const float offsetY, const float offsetZ, ImVec2& screenPosition) const
	{
		return ProjectToScreen(Vector3(point.x + offsetX, point.y + offsetY, point.z + offsetZ), screenPosition);
	}

	const SplinePoint* SplineEditController::GetSelectedCurvePoint(const std::vector<PathSpline*>& splines) const
	{
		if (m_selectedSpline < 0 || m_selectedSpline >= static_cast<int32>(splines.size()))
		{
			return nullptr;
		}

		const PathSpline& spline = *splines[m_selectedSpline];
		if (!HasTangents(spline) || m_selectedPoint < 0 || m_selectedPoint >= static_cast<int32>(spline.points.size()))
		{
			return nullptr;
		}

		return &spline.points[m_selectedPoint];
	}

	bool SplineEditController::HitTestTangentHandle(const float x, const float y, const std::vector<PathSpline*>& splines, TangentHandle& handle) const
	{
		const SplinePoint* point = GetSelectedCurvePoint(splines);
		if (!point)
		{
			return false;
		}

		ImVec2 pointScreen;
		if (!ProjectSplinePoint(*splines[m_selectedSpline], *point, pointScreen))
		{
			return false;
		}

		const SplineTangents t = GetEffectiveTangents(*splines[m_selectedSpline], static_cast<size_t>(m_selectedPoint));

		float best = PointPickRadius;
		bool found = false;
		const struct { TangentHandle which; float dx; float dy; float dz; } candidates[] = {
			{ TangentHandle::In, t.inX, t.inY, t.inZ },
			{ TangentHandle::Out, t.outX, t.outY, t.outZ },
		};
		for (const auto& candidate : candidates)
		{
			ImVec2 screen;
			if (!ProjectHandle(*point, candidate.dx, candidate.dy, candidate.dz, screen))
			{
				continue;
			}

			// A handle on top of its point would make the point ungrabbable.
			if (std::hypot(screen.x - pointScreen.x, screen.y - pointScreen.y) < PointPickRadius)
			{
				continue;
			}

			const float distance = std::hypot(screen.x - x, screen.y - y);
			if (distance < best)
			{
				best = distance;
				handle = candidate.which;
				found = true;
			}
		}
		return found;
	}

	bool SplineEditController::GetDragAnchor(const std::vector<PathSpline*>& splines, float& x, float& y, float& z) const
	{
		if (m_selectedSpline < 0 || m_selectedSpline >= static_cast<int32>(splines.size()))
		{
			return false;
		}

		const PathSpline& spline = *splines[m_selectedSpline];
		if (m_selectedPoint < 0 || m_selectedPoint >= static_cast<int32>(spline.points.size()))
		{
			return false;
		}

		const SplinePoint& point = spline.points[m_selectedPoint];
		const Vector3 position = GetPointPosition(spline, point);
		x = position.x;
		y = position.y;
		z = position.z;
		if (m_dragHandle != TangentHandle::None)
		{
			const SplineTangents t = GetEffectiveTangents(spline, static_cast<size_t>(m_selectedPoint));
			const bool isIn = m_dragHandle == TangentHandle::In;
			x += isIn ? t.inX : t.outX;
			y += isIn ? t.inY : t.outY;
			z += isIn ? t.inZ : t.outZ;
		}
		return true;
	}

	bool SplineEditController::ResetTangents(PathSpline& spline, const size_t pointIndex)
	{
		if (pointIndex >= spline.points.size() || spline.points[pointIndex].autoTangents)
		{
			return false;
		}

		SplinePoint& point = spline.points[pointIndex];
		point.autoTangents = true;
		point.inX = point.inY = point.inZ = point.outX = point.outY = point.outZ = 0.0f;
		return true;
	}

	bool SplineEditController::ResetAllTangents(PathSpline& spline)
	{
		bool changed = false;
		for (size_t i = 0; i < spline.points.size(); ++i)
		{
			changed = ResetTangents(spline, i) || changed;
		}
		return changed;
	}

	void SplineEditController::Draw(ImDrawList& drawList, const ImVec2& viewportMin, const ImVec2& viewportSize,
		const std::vector<const PathSpline*>& splines, const std::vector<Style>& styles)
	{
		m_viewProj = m_camera.GetProjectionMatrix() * m_camera.GetViewMatrix();
		m_viewportMin = viewportMin;
		m_viewportSize = viewportSize;

		for (int32 s = 0; s < static_cast<int32>(splines.size()) && s < static_cast<int32>(styles.size()); ++s)
		{
			const PathSpline& spline = *splines[s];
			const Style& style = styles[s];
			const auto& points = spline.points;
			const bool selectedSpline = s == m_selectedSpline;
			const ImU32 lineColor = style.lineColor;
			const ImU32 mainColor = selectedSpline ? style.selectedColor : lineColor;
			// Ribbon: the line colour, more translucent, a little less so on the selected spline.
			const ImU32 bankColor = (mainColor & 0x00FFFFFFu) | ((selectedSpline ? 120u : 80u) << 24);

			if (spline.interpolation == SplineInterpolation::Linear)
			{
				const size_t segments = GetSegmentCount(spline);
				for (size_t p = 0; p < segments; ++p)
				{
					const SplinePoint& a = points[p];
					const SplinePoint& b = points[(p + 1) % points.size()];
					const float dx = b.x - a.x;
					const float dz = b.z - a.z;
					const float length = std::hypot(dx, dz);
					if (length <= 0.0f)
					{
						continue;
					}

					// Left of the direction is (-dz, dx), as in the river flow mapping.
					const float nx = -dz / length;
					const float nz = dx / length;

					ImVec2 sa, sb;
					if (ProjectPlanePoint(a.x, a.z, sa) && ProjectPlanePoint(b.x, b.z, sb))
					{
						drawList.AddLine(sa, sb, mainColor, selectedSpline ? 3.0f : 2.0f);
					}

					if (style.drawWidthRibbon)
					{
						for (const float sideSign : { -1.0f, 1.0f })
						{
							const float ax = a.x + nx * a.width * sideSign;
							const float az = a.z + nz * a.width * sideSign;
							const float bx = b.x + nx * b.width * sideSign;
							const float bz = b.z + nz * b.width * sideSign;
							ImVec2 s0, s1;
							if (ProjectPlanePoint(ax, az, s0) && ProjectPlanePoint(bx, bz, s1))
							{
								drawList.AddLine(s0, s1, bankColor, 1.0f);
							}
						}
					}

					if (style.drawDirection)
					{
						// Chevrons every ~15 m point along the direction.
						const int32 chevrons = std::max(1, static_cast<int32>(length / 15.0f));
						for (int32 c = 0; c < chevrons; ++c)
						{
							const float t = (static_cast<float>(c) + 0.5f) / static_cast<float>(chevrons);
							const float cx = a.x + dx * t;
							const float cz = a.z + dz * t;
							const float size = std::min(4.0f, (a.width + (b.width - a.width) * t) * 0.5f);
							const float fx = dx / length;
							const float fz = dz / length;
							const float tipX = cx + fx * size;
							const float tipZ = cz + fz * size;
							const float baseHeight = m_height ? m_height(cx, cz) : 0.0f;
							ImVec2 sTip, sLeft, sRight;
							if (ProjectPlanePoint(tipX, tipZ, sTip) &&
								ProjectToScreen(Vector3(cx - fx * size + nx * size, baseHeight, cz - fz * size + nz * size), sLeft) &&
								ProjectToScreen(Vector3(cx - fx * size - nx * size, baseHeight, cz - fz * size - nz * size), sRight))
							{
								drawList.AddLine(sLeft, sTip, mainColor, 2.0f);
								drawList.AddLine(sRight, sTip, mainColor, 2.0f);
							}
						}
					}
				}
			}
			else if (points.size() >= 2)
			{
				const SampledPath sampled(spline, CurveDrawStep);
				const auto& samples = sampled.GetSamples();
				const size_t count = samples.size() - (sampled.IsClosed() ? 0 : 1);
				for (size_t i = 0; i < count; ++i)
				{
					const SplineSample& a = samples[i];
					const SplineSample& b = samples[(i + 1) % samples.size()];
					ImVec2 sa, sb;
					if (ProjectToScreen(Vector3(a.x, a.y, a.z), sa) && ProjectToScreen(Vector3(b.x, b.y, b.z), sb))
					{
						drawList.AddLine(sa, sb, mainColor, selectedSpline ? 3.0f : 2.0f);
					}
				}
			}

			for (int32 p = 0; p < static_cast<int32>(points.size()); ++p)
			{
				ImVec2 screen;
				if (!ProjectSplinePoint(spline, points[p], screen))
				{
					continue;
				}

				// A faint drop line to the ground makes a 3D point's height readable.
				ImVec2 ground;
				if (HasTangents(spline) && ProjectPlanePoint(points[p].x, points[p].z, ground))
				{
					drawList.AddLine(screen, ground, (mainColor & 0x00FFFFFFu) | (70u << 24), 1.0f);
				}

				const bool selectedPoint = selectedSpline && p == m_selectedPoint;
				const float radius = selectedPoint ? 7.0f : 5.0f;
				drawList.AddCircleFilled(screen, radius, selectedPoint ? style.pointColor : mainColor);
				drawList.AddCircle(screen, radius, IM_COL32(0, 0, 0, 200), 0, 1.5f);

				if (p == 0 && !style.label.empty())
				{
					drawList.AddText(ImVec2(screen.x + 8.0f, screen.y - 8.0f), mainColor, style.label.c_str());
				}
			}

			// Tangent handles of the selected point: a line from the point to each handle; a manual
			// point's handles are filled, an automatic point's hollow.
			if (selectedSpline && HasTangents(spline) && m_selectedPoint >= 0 && m_selectedPoint < static_cast<int32>(points.size()))
			{
				const SplinePoint& point = points[m_selectedPoint];
				const SplineTangents t = GetEffectiveTangents(spline, static_cast<size_t>(m_selectedPoint));

				ImVec2 pointScreen;
				if (ProjectSplinePoint(spline, point, pointScreen))
				{
					const struct { float dx; float dy; float dz; ImU32 color; } handles[] = {
						{ t.inX, t.inY, t.inZ, InHandleColor },
						{ t.outX, t.outY, t.outZ, OutHandleColor },
					};
					for (const auto& handle : handles)
					{
						ImVec2 handleScreen;
						if (!ProjectHandle(point, handle.dx, handle.dy, handle.dz, handleScreen))
						{
							continue;
						}

						drawList.AddLine(pointScreen, handleScreen, handle.color, 1.5f);
						if (point.autoTangents)
						{
							drawList.AddCircleFilled(handleScreen, HandleRadius, IM_COL32(0, 0, 0, 160));
							drawList.AddCircle(handleScreen, HandleRadius, handle.color, 0, 2.0f);
						}
						else
						{
							drawList.AddCircleFilled(handleScreen, HandleRadius, handle.color);
							drawList.AddCircle(handleScreen, HandleRadius, IM_COL32(0, 0, 0, 200), 0, 1.5f);
						}
					}
				}
			}
		}
	}

	bool SplineEditController::HitTestPoint(const float x, const float y, const std::vector<PathSpline*>& splines,
		int32& splineIndex, int32& pointIndex) const
	{
		float best = PointPickRadius;
		bool found = false;
		for (int32 s = 0; s < static_cast<int32>(splines.size()); ++s)
		{
			const auto& points = splines[s]->points;
			for (int32 p = 0; p < static_cast<int32>(points.size()); ++p)
			{
				ImVec2 screen;
				if (!ProjectSplinePoint(*splines[s], points[p], screen))
				{
					continue;
				}

				// Points of the selected spline win ties, so overlapping splines stay editable.
				const float distance = std::hypot(screen.x - x, screen.y - y) - (s == m_selectedSpline ? 0.5f : 0.0f);
				if (distance < best)
				{
					best = distance;
					splineIndex = s;
					pointIndex = p;
					found = true;
				}
			}
		}
		return found;
	}

	bool SplineEditController::HitTestSegment(const float x, const float y, const std::vector<PathSpline*>& splines,
		int32& splineIndex, int32& pointIndex) const
	{
		const ImVec2 mouse(x, y);
		float best = PointPickRadius;
		bool found = false;
		for (int32 s = 0; s < static_cast<int32>(splines.size()); ++s)
		{
			const auto& points = splines[s]->points;
			const size_t segments = GetSegmentCount(*splines[s]);
			for (size_t p = 0; p < segments; ++p)
			{
				const SplinePoint& a = points[p];
				const SplinePoint& b = points[(p + 1) % points.size()];
				ImVec2 sa, sb;
				if (!ProjectSplinePoint(*splines[s], a, sa) || !ProjectSplinePoint(*splines[s], b, sb))
				{
					continue;
				}

				const float distance = DistanceToSegment(mouse, sa, sb);
				if (distance < best)
				{
					best = distance;
					splineIndex = s;
					pointIndex = static_cast<int32>(p);
					found = true;
				}
			}
		}
		return found;
	}

	bool SplineEditController::OnMouseDown(const float x, const float y, std::vector<PathSpline*>& splines, const bool alt, const Vector3* worldHit)
	{
		// Alt+click is the insert gesture; a handle lying over a segment must not swallow it.
		TangentHandle handle = TangentHandle::None;
		if (!alt && HitTestTangentHandle(x, y, splines, handle))
		{
			BeginHandleDrag(handle);
			return true;
		}

		int32 splineIndex = -1;
		int32 pointIndex = -1;
		if (HitTestPoint(x, y, splines, splineIndex, pointIndex))
		{
			m_selectedSpline = splineIndex;
			m_selectedPoint = pointIndex;
			BeginDrag(false);
			return true;
		}

		// Linear splines (rivers) need the terrain hit to insert, as always; curved ones insert on the curve.
		if (alt && HitTestSegment(x, y, splines, splineIndex, pointIndex) &&
			(worldHit != nullptr || HasTangents(*splines[splineIndex])))
		{
			auto& points = splines[splineIndex]->points;
			const SplinePoint a = points[pointIndex];
			const SplinePoint b = points[(pointIndex + 1) % points.size()];

			SplinePoint inserted;
			inserted.width = (a.width + b.width) * 0.5f;

			// A curved (3D) spline gets the point on the drawn curve nearest the cursor, height included:
			// the terrain under the cursor can lie far behind a raised curve.
			bool placed = false;
			if (HasTangents(*splines[splineIndex]))
			{
				const SampledPath sampled(*splines[splineIndex], CurveDrawStep * 0.25f);
				float best = std::numeric_limits<float>::max();
				for (const SplineSample& sample : sampled.GetSamples())
				{
					ImVec2 screen;
					if (sampled.SegmentAtDistance(sample.distance) != static_cast<uint32>(pointIndex) ||
						!ProjectToScreen(Vector3(sample.x, sample.y, sample.z), screen))
					{
						continue;
					}

					const float distance = std::hypot(screen.x - x, screen.y - y);
					if (distance < best)
					{
						best = distance;
						inserted.x = sample.x;
						inserted.y = sample.y;
						inserted.z = sample.z;
						placed = true;
					}
				}
			}

			if (!placed)
			{
				if (worldHit == nullptr)
				{
					return false;
				}
				inserted.x = worldHit->x;
				// Linear points keep the default height, as before heights existed.
				inserted.y = HasTangents(*splines[splineIndex]) ? worldHit->y : 0.0f;
				inserted.z = worldHit->z;
			}
			points.insert(points.begin() + pointIndex + 1, inserted);

			m_selectedSpline = splineIndex;
			m_selectedPoint = pointIndex + 1;

			// The new point follows the mouse until release, so click-and-drag places it precisely.
			BeginDrag(true);
			return true;
		}

		return false;
	}

	bool SplineEditController::OnMouseMoved(const float x, const float y, std::vector<PathSpline*>& splines)
	{
		if (!m_dragging || m_selectedSpline < 0 || m_selectedSpline >= static_cast<int32>(splines.size()))
		{
			return false;
		}

		auto& points = splines[m_selectedSpline]->points;
		if (m_selectedPoint < 0 || m_selectedPoint >= static_cast<int32>(points.size()))
		{
			return false;
		}

		SplinePoint& point = points[m_selectedPoint];
		if (m_dragHandle != TangentHandle::None)
		{
			if (!HasTangents(*splines[m_selectedSpline]))
			{
				return false;
			}

			const float offsetX = x - point.x;
			const float offsetZ = y - point.z;
			float inX, inZ, outX, outZ;
			GetEffectiveTangents(*splines[m_selectedSpline], static_cast<size_t>(m_selectedPoint), inX, inZ, outX, outZ);
			const bool isIn = m_dragHandle == TangentHandle::In;
			if ((isIn ? inX : outX) == offsetX && (isIn ? inZ : outZ) == offsetZ)
			{
				return false;
			}

			// Materialise the current tangents first, so the other handle stays where it is.
			MakeTangentsManual(*splines[m_selectedSpline], static_cast<size_t>(m_selectedPoint));
			if (isIn)
			{
				point.inX = offsetX;
				point.inZ = offsetZ;
			}
			else
			{
				point.outX = offsetX;
				point.outZ = offsetZ;
			}
			m_dragMoved = true;
			return true;
		}

		if (point.x == x && point.z == y)
		{
			return false;
		}

		point.x = x;
		point.z = y;
		m_dragMoved = true;
		return true;
	}

	void SplineEditController::OnMouseUp()
	{
		m_dragging = false;
		m_dragMoved = false;
		m_dragHandle = TangentHandle::None;
	}

	bool SplineEditController::DeleteSelectedPoint(std::vector<PathSpline*>& splines, const uint32 minPoints)
	{
		if (m_selectedSpline < 0 || m_selectedSpline >= static_cast<int32>(splines.size()))
		{
			return false;
		}

		auto& points = splines[m_selectedSpline]->points;
		if (m_selectedPoint < 0 || m_selectedPoint >= static_cast<int32>(points.size()))
		{
			return false;
		}

		if (points.size() <= minPoints)
		{
			return false;
		}

		points.erase(points.begin() + m_selectedPoint);
		m_selectedPoint = points.empty() ? -1 : std::min(m_selectedPoint, static_cast<int32>(points.size()) - 1);
		m_dragging = false;
		m_dragHandle = TangentHandle::None;
		return true;
	}
}
