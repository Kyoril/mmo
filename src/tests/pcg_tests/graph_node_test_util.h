// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "catch.hpp"

#include "pcg/graph/evaluator.h"
#include "pcg/graph/node_registry.h"
#include "pcg/pcg_random.h"

#include <cmath>
#include <utility>
#include <vector>

namespace
{
	/// Runs a node type's executor directly with the declared defaults filled in and every declared output pin present.
	mmo::pcg::graph::NodeOutput ExecNode(const mmo::String& type, const mmo::pcg::graph::AttributeSet& settings, const std::map<mmo::String, mmo::pcg::graph::Collection>& inputs, const mmo::pcg::graph::EvalContext& ctx, const mmo::uint64 seed = 1234)
	{
		using namespace mmo::pcg::graph;
		const NodeTypeDef* def = NodeRegistry::Default().Find(type);
		REQUIRE(def != nullptr);
		AttributeSet effective = settings;
		for (const SettingDef& s : def->settings)
		{
			effective.emplace(s.name, s.defaultValue);
		}
		NodeOutput out = def->execute(ctx, seed, effective, inputs);
		for (const PinDef& pin : def->outputs)
		{
			out.pins[pin.name];
		}
		return out;
	}

	mmo::pcg::graph::DataItem MakeSplineItem(const std::vector<mmo::SplinePoint>& pts, const bool closed)
	{
		mmo::pcg::graph::DataItem item;
		item.kind = closed ? mmo::pcg::graph::ItemKind::Area : mmo::pcg::graph::ItemKind::Spline;
		item.spline.points = pts;
		item.spline.closed = closed;
		return item;
	}

	mmo::pcg::graph::DataItem MakeSquare(const float x0, const float z0, const float size)
	{
		return MakeSplineItem({ { x0, z0 }, { x0 + size, z0 }, { x0 + size, z0 + size }, { x0, z0 + size } }, true);
	}

	/// A Points item with one point per (x, z), at y = 0, seeded from its index.
	mmo::pcg::graph::DataItem MakePoints(const std::vector<std::pair<float, float>>& xz, const mmo::uint64 seedBase = 77)
	{
		mmo::pcg::graph::DataItem item;
		item.kind = mmo::pcg::graph::ItemKind::Points;
		for (size_t i = 0; i < xz.size(); ++i)
		{
			mmo::pcg::graph::Point p;
			p.position = mmo::Vector3(xz[i].first, 0.0f, xz[i].second);
			p.seed = mmo::pcg::Hash(seedBase, static_cast<mmo::int64>(i));
			item.points.points.push_back(p);
		}
		return item;
	}

	/// A Points item on a regular grid [x0, x1] x [z0, z1] with the given step.
	mmo::pcg::graph::DataItem MakeGrid(const float x0, const float z0, const float x1, const float z1, const float step)
	{
		std::vector<std::pair<float, float>> xz;
		for (float z = z0; z <= z1 + 1.0e-4f; z += step)
		{
			for (float x = x0; x <= x1 + 1.0e-4f; x += step)
			{
				xz.emplace_back(x, z);
			}
		}
		return MakePoints(xz);
	}

	size_t CountPoints(const mmo::pcg::graph::Collection& c)
	{
		size_t n = 0;
		for (const mmo::pcg::graph::DataItem& item : c)
		{
			n += item.points.points.size();
		}
		return n;
	}

	/// All points of a collection, in order.
	std::vector<mmo::pcg::graph::Point> AllPoints(const mmo::pcg::graph::Collection& c)
	{
		std::vector<mmo::pcg::graph::Point> result;
		for (const mmo::pcg::graph::DataItem& item : c)
		{
			result.insert(result.end(), item.points.points.begin(), item.points.points.end());
		}
		return result;
	}

	float FloatAttr(const mmo::pcg::graph::DataItem& item, const size_t index, const mmo::String& name)
	{
		const mmo::pcg::graph::AttributeValue* v = item.points.GetAttribute(index, name);
		REQUIRE(v != nullptr);
		REQUIRE(std::holds_alternative<float>(*v));
		return std::get<float>(*v);
	}
}
