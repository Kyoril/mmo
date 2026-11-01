// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graph_data.h"

#include "math/path_spline.h"
#include "math/quaternion.h"
#include "math/vector3.h"

#include <set>
#include <vector>

namespace mmo::pcg::graph
{
	/// @brief What kind of geometry a control type is placed as.
	enum class ControlShape : uint8
	{
		/// @brief A closed spline (an area), at least 3 points.
		ClosedLoop,

		/// @brief An open spline (a path), at least 2 points.
		OpenSpline,

		/// @brief A box volume (position, rotation, half extents).
		Volume,

		/// @brief A single transform (position, rotation).
		Point
	};

	/// @brief Type of a control property. Each maps to one attribute type:
	///        Float -> float, Int -> int64, Bool -> bool, Enum/String/Asset -> String, Color -> Vector3 (rgb),
	///        AssetList -> String holding "asset|weight;asset|weight".
	enum class PropertyType : uint8
	{
		Float,
		Int,
		Bool,
		Enum,
		String,
		Asset,
		AssetList,
		Color
	};

	/// @brief Gets the attribute type a property type is stored as.
	AttributeType AttributeTypeOf(PropertyType type);

	/// @brief Definition of one property of a control type.
	struct PropertyDef
	{
		String name;
		PropertyType type = PropertyType::Float;

		/// @brief Default value; must have the attribute type of `type`.
		AttributeValue defaultValue;

		/// @brief Numeric range (Float, Int); clamping applies only when max > min.
		float min = 0;
		float max = 0;

		/// @brief Allowed values of an Enum property.
		std::vector<String> enumValues;
		String description;
	};

	/// @brief A named group of properties (a section in the editor).
	struct PropertyGroup
	{
		String name;
		std::vector<PropertyDef> properties;
	};

	/// @brief A data-driven control type (.hctl).
	struct ControlType
	{
		String name;
		ControlShape shape = ControlShape::ClosedLoop;
		std::set<String> defaultTags;
		std::vector<PropertyGroup> groups;
	};

	/// @brief A placed control (.hpcgc).
	struct ControlInstance
	{
		uint64 id = 0;

		/// @brief Path of the control type this instance was placed from.
		String typePath;
		String name;
		std::set<String> extraTags;

		/// @brief Loops and splines: the shape, one stable id per point, and the next id to hand out.
		PathSpline spline;
		std::vector<uint64> pointIds;
		uint64 nextPointId = 1;

		/// @brief Volumes and points.
		Vector3 position;
		Quaternion rotation;
		Vector3 halfExtents = Vector3(5, 5, 5);

		/// @brief Property overrides by property name.
		AttributeSet overrides;
	};

	/// @brief Type and instance merged: tags = defaultTags union extraTags, attributes = defaults overridden
	///        (validated and clamped). The pointers must outlive the resolved control.
	struct ResolvedControl
	{
		const ControlType* type = nullptr;
		const ControlInstance* instance = nullptr;
		std::set<String> tags;
		AttributeSet attributes;
	};

	/// @brief Parses a .hctl control type file (JSON, "version": 1). Unknown keys are ignored.
	/// @param out Receives the type on success; left untouched on failure.
	/// @param error Receives a message on failure.
	bool ParseControlType(const String& json, ControlType& out, String& error);

	/// @brief Writes a control type as .hctl text (deterministic key order, 2-space indent).
	String WriteControlType(const ControlType& type);

	/// @brief Parses a .hpcgc placed control file (JSON, "version": 1). Unknown keys are ignored.
	bool ParseControlInstance(const String& json, ControlInstance& out, String& error);

	/// @brief Writes a placed control as .hpcgc text (deterministic key order, 2-space indent).
	String WriteControlInstance(const ControlInstance& instance);

	/// @brief Merges a type and an instance. Overrides of unknown properties are kept and warned about;
	///        type mismatches, non-finite numbers and enum values outside the list fall back to the default
	///        with a warning; numbers clamp to [min,max] when max > min (also the defaults).
	/// @param warnings Receives one message per problem (also the refusal reason).
	/// @return False (out untouched) if the instance's geometry does not fit the type's shape: a loop needs
	///         a closed spline with at least 3 points, an open spline an open one with at least 2, and
	///         spline shapes need exactly one point id per point.
	bool ResolveControl(const ControlType& type, const ControlInstance& inst, ResolvedControl& out, std::vector<String>& warnings);

	/// @brief Order-sensitive hash over everything of every control that can influence an evaluation: the
	///        type (name, shape, default tags, all property definitions), the instance (id, type path, name,
	///        extra tags, spline with all point fields, point ids, next point id, transform, half extents,
	///        overrides) and the resolved tags and attributes. Intended for EvalContext::controlsRevision.
	uint64 ControlsHash(const std::vector<ResolvedControl>& controls);
}
