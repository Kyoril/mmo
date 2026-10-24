// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spline_edit_controller.h"

#include "math/vector4.h"
#include "scene_graph/camera.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace
	{
		/// Pick radius of a control point in screen pixels.
		constexpr float PointPickRadius = 9.0f;

		/// Maximum polyline step when drawing curved splines, in world units.
		constexpr float CurveDrawStep = 4.0f;

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
					if (ProjectPlanePoint(a.x, a.z, sa) && ProjectPlanePoint(b.x, b.z, sb))
					{
						drawList.AddLine(sa, sb, mainColor, selectedSpline ? 3.0f : 2.0f);
					}
				}
			}

			for (int32 p = 0; p < static_cast<int32>(points.size()); ++p)
			{
				ImVec2 screen;
				if (!ProjectPlanePoint(points[p].x, points[p].z, screen))
				{
					continue;
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
				if (!ProjectPlanePoint(points[p].x, points[p].z, screen))
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
				if (!ProjectPlanePoint(a.x, a.z, sa) || !ProjectPlanePoint(b.x, b.z, sb))
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
		int32 splineIndex = -1;
		int32 pointIndex = -1;
		if (HitTestPoint(x, y, splines, splineIndex, pointIndex))
		{
			m_selectedSpline = splineIndex;
			m_selectedPoint = pointIndex;
			BeginDrag(false);
			return true;
		}

		if (alt && worldHit != nullptr && HitTestSegment(x, y, splines, splineIndex, pointIndex))
		{
			auto& points = splines[splineIndex]->points;
			const SplinePoint a = points[pointIndex];
			const SplinePoint b = points[(pointIndex + 1) % points.size()];

			SplinePoint inserted;
			inserted.x = worldHit->x;
			inserted.z = worldHit->z;
			inserted.width = (a.width + b.width) * 0.5f;
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
		return true;
	}
}
