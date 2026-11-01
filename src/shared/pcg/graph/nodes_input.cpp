// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Input nodes. Each declares the context parts it reads (contextDeps): GetControls Controls + Region,
// GraphParameters Parameters, GetRegion Region.
//
// GetControls  settings: tags (comma list, a control needs all of them), excludeTags (comma list, a control
//              with any of them is dropped), controlType (type path; empty = any). Output "Out": one item per
//              matching control whose x/z bounds intersect the region's bounds (no culling when the region
//              has fewer than 3 points), sorted by control id (stable for equal ids). Shape mapping:
//              ClosedLoop -> Area, OpenSpline -> Spline, Volume -> Area (the box footprint, see below) with the
//              item attribute volumeHeight, Point -> Points with one point. Every item carries the control's
//              resolved tags and attributes; sourceId = control id. Volume footprints use only the yaw of the
//              rotation (the rotated local x axis projected onto x/z); corners in local order (-x,-z), (+x,-z),
//              (+x,+z), (-x,+z) at the height of the box bottom; volumeHeight = 2 * halfExtents.y.
//              A Point control's point seed is Hash(nodeSeed, control id, 0, salt).
// GraphParameters  output "Out": one Params item holding ctx.parameters, which the evaluator has merged over
//              the graph's parameter defaults; the merge warnings are reported as node warnings.
// GetRegion    output "Out": the instance region as one closed Area item (sourceId 0), or nothing when the
//              region has fewer than 3 points.

#include "builtin_nodes.h"
#include "controls.h"
#include "evaluator.h"
#include "node_registry.h"
#include "node_util.h"

#include "pcg/pcg_random.h"

#include <algorithm>
#include <cmath>

namespace mmo::pcg::graph
{
	namespace
	{
		constexpr uint64 PointControlSalt = 0x7c0a5eedull;

		/// Footprint corners of a volume control (yaw only).
		std::vector<SplinePoint> VolumeFootprint(const ControlInstance& inst)
		{
			const Vector3 axis = inst.rotation * Vector3(1, 0, 0);
			float ax = axis.x;
			float az = axis.z;
			const float len = std::sqrt(ax * ax + az * az);
			if (!(len > 1.0e-6f))
			{
				ax = 1.0f;
				az = 0.0f;
			}
			else
			{
				ax /= len;
				az /= len;
			}

			// Yaw maps local x to (c, -s) and local z to (s, c) = (-x.z, x.x).
			const float zx = -az;
			const float zz = ax;
			const float hx = std::fabs(inst.halfExtents.x);
			const float hz = std::fabs(inst.halfExtents.z);
			const float bottom = inst.position.y - std::fabs(inst.halfExtents.y);

			std::vector<SplinePoint> corners;
			const float signs[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
			for (const auto& s : signs)
			{
				SplinePoint p;
				p.x = inst.position.x + s[0] * hx * ax + s[1] * hz * zx;
				p.z = inst.position.z + s[0] * hx * az + s[1] * hz * zz;
				p.y = bottom;
				corners.push_back(p);
			}
			return corners;
		}

		bool HasAll(const std::set<String>& tags, const std::vector<String>& required)
		{
			for (const String& tag : required)
			{
				if (tags.count(tag) == 0)
				{
					return false;
				}
			}
			return true;
		}

		bool HasAny(const std::set<String>& tags, const std::vector<String>& excluded)
		{
			for (const String& tag : excluded)
			{
				if (tags.count(tag) != 0)
				{
					return true;
				}
			}
			return false;
		}

		NodeOutput ExecuteGetControls(const EvalContext& ctx, const uint64 nodeSeed, const AttributeSet& settings, const std::map<String, Collection>&)
		{
			NodeOutput out;
			Collection& items = out.pins["Out"];
			if (ctx.controls == nullptr)
			{
				return out;
			}

			const std::vector<String> tags = SplitList(SettingString(settings, "tags", ""));
			const std::vector<String> excludeTags = SplitList(SettingString(settings, "excludeTags", ""));
			const String controlType = SettingString(settings, "controlType", "");

			Bounds2D region;
			if (ctx.region.points.size() >= 3)
			{
				region = SplineBounds(ctx.region);
			}

			std::vector<const ResolvedControl*> matches;
			for (const ResolvedControl& control : *ctx.controls)
			{
				if (control.type == nullptr || control.instance == nullptr)
				{
					WarnOnce(out, "skipped a control without type or instance");
					continue;
				}
				if (!controlType.empty() && control.instance->typePath != controlType)
				{
					continue;
				}
				if (!HasAll(control.tags, tags) || HasAny(control.tags, excludeTags))
				{
					continue;
				}
				matches.push_back(&control);
			}

			std::stable_sort(matches.begin(), matches.end(), [](const ResolvedControl* a, const ResolvedControl* b)
			{
				return a->instance->id < b->instance->id;
			});

			for (const ResolvedControl* control : matches)
			{
				const ControlInstance& inst = *control->instance;
				DataItem item;
				item.tags = control->tags;
				item.attributes = control->attributes;
				item.sourceId = inst.id;

				Bounds2D bounds;
				switch (control->type->shape)
				{
				case ControlShape::ClosedLoop:
					item.kind = ItemKind::Area;
					item.spline = inst.spline;
					item.spline.closed = true;
					item.pointIds = inst.pointIds;
					bounds = SplineBounds(item.spline);
					break;
				case ControlShape::OpenSpline:
					item.kind = ItemKind::Spline;
					item.spline = inst.spline;
					item.spline.closed = false;
					item.pointIds = inst.pointIds;
					bounds = SplineBounds(item.spline);
					break;
				case ControlShape::Volume:
					item.kind = ItemKind::Area;
					item.spline.closed = true;
					item.spline.interpolation = SplineInterpolation::Linear;
					item.spline.points = VolumeFootprint(inst);
					item.attributes["volumeHeight"] = 2.0f * std::fabs(inst.halfExtents.y);
					bounds = SplineBounds(item.spline);
					break;
				case ControlShape::Point:
				{
					item.kind = ItemKind::Points;
					Point p;
					p.position = inst.position;
					p.rotation = inst.rotation;
					p.seed = Hash(nodeSeed, static_cast<int64>(inst.id), 0, PointControlSalt);
					item.points.points.push_back(p);
					bounds.Add(inst.position.x, inst.position.z);
					break;
				}
				}

				if (!SaneBounds(bounds))
				{
					WarnOnce(out, "skipped control " + std::to_string(inst.id) + ": empty, non-finite or out of range geometry");
					continue;
				}
				if (region.valid && !bounds.Intersects(region))
				{
					continue;
				}
				items.push_back(std::move(item));
			}
			return out;
		}

		NodeOutput ExecuteGraphParameters(const EvalContext& ctx, uint64, const AttributeSet&, const std::map<String, Collection>&)
		{
			NodeOutput out;
			DataItem item;
			item.kind = ItemKind::Params;
			item.attributes = ctx.parameters;
			out.pins["Out"].push_back(std::move(item));
			out.warnings = ctx.parameterWarnings;
			return out;
		}

		NodeOutput ExecuteGetRegion(const EvalContext& ctx, uint64, const AttributeSet&, const std::map<String, Collection>&)
		{
			NodeOutput out;
			Collection& items = out.pins["Out"];
			if (ctx.region.points.size() < 3)
			{
				return out;
			}
			DataItem item;
			item.kind = ItemKind::Area;
			item.spline = ctx.region;
			item.spline.closed = true;
			items.push_back(std::move(item));
			return out;
		}
	}

	void RegisterInputNodes(NodeRegistry& registry)
	{
		{
			NodeTypeDef def;
			def.type = "GetControls";
			def.category = "Input";
			def.title = "Get Controls";
			def.outputs.push_back(PinDef{ "Out", { ItemKind::Area, ItemKind::Spline, ItemKind::Points } });
			def.settings.push_back(SettingDef{ "tags", String(), 0, 0, {}, "Comma separated tags; a control needs all of them" });
			def.settings.push_back(SettingDef{ "excludeTags", String(), 0, 0, {}, "Comma separated tags; a control with any of them is dropped" });
			def.settings.push_back(SettingDef{ "controlType", String(), 0, 0, {}, "Control type path (empty = any type)" });
			def.contextDeps = context_dep::Controls | context_dep::Region;
			def.execute = ExecuteGetControls;
			registry.Register(std::move(def));
		}
		{
			NodeTypeDef def;
			def.type = "GraphParameters";
			def.category = "Input";
			def.title = "Graph Parameters";
			def.outputs.push_back(PinDef{ "Out", { ItemKind::Params } });
			def.contextDeps = context_dep::Parameters;
			def.execute = ExecuteGraphParameters;
			registry.Register(std::move(def));
		}
		{
			NodeTypeDef def;
			def.type = "GetRegion";
			def.category = "Input";
			def.title = "Get Region";
			def.outputs.push_back(PinDef{ "Out", { ItemKind::Area } });
			def.contextDeps = context_dep::Region;
			def.execute = ExecuteGetRegion;
			registry.Register(std::move(def));
		}
	}
}
