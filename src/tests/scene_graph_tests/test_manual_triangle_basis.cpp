// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "scene_graph/manual_render_object.h"

using namespace mmo;

namespace
{
	/// Builds a triangle lying in the XZ plane, the shape every water sub-quad is made of.
	ManualTriangleListOperation::Triangle MakeHorizontalTriangle()
	{
		return ManualTriangleListOperation::Triangle(
			Vector3(0.0f, 0.0f, 0.0f),
			Vector3(1.0f, 0.0f, 0.0f),
			Vector3(0.0f, 0.0f, 1.0f));
	}
}

TEST_CASE("ManualTriangle_Basis_Defaults_To_UnitY", "[manual_render_object]")
{
	// Every existing caller - debug geometry, the axis display, collision overlays, the
	// navmesh visualiser - never sets a basis. They must keep byte-identical vertex data.
	const ManualTriangleListOperation::Triangle triangle = MakeHorizontalTriangle();

	for (uint8 i = 0; i < 3; ++i)
	{
		CHECK(triangle.GetNormal(i) == Vector3::UnitY);
		CHECK(triangle.GetTangent(i) == Vector3::UnitY);
		CHECK(triangle.GetBinormal(i) == Vector3::UnitY);
	}
}

TEST_CASE("ManualTriangle_Basis_Is_Per_Vertex", "[manual_render_object]")
{
	ManualTriangleListOperation::Triangle triangle = MakeHorizontalTriangle();

	triangle.SetNormal(0, Vector3::UnitY);
	triangle.SetNormal(1, Vector3::NegativeUnitY);

	CHECK(triangle.GetNormal(0) == Vector3::UnitY);
	CHECK(triangle.GetNormal(1) == Vector3::NegativeUnitY);

	// Vertices that were not touched keep the default.
	CHECK(triangle.GetNormal(2) == Vector3::UnitY);
}

TEST_CASE("ManualTriangle_Top_Face_Basis_Is_Orthonormal", "[manual_render_object]")
{
	// The water surface case. Before this change all three vectors were UnitY, so the basis
	// GetWorldNormal() builds in every generated pixel shader collapsed to a line and any
	// sampled normal map resolved to nonsense. Water is the only shipping consumer that
	// samples one, which is why the water material has never looked right.
	ManualTriangleListOperation::Triangle triangle = MakeHorizontalTriangle();

	for (uint8 i = 0; i < 3; ++i)
	{
		triangle.SetNormal(i, Vector3::UnitY);
		triangle.SetTangent(i, Vector3::UnitX);
		triangle.SetBinormal(i, Vector3::UnitZ);
	}

	for (uint8 i = 0; i < 3; ++i)
	{
		CHECK(triangle.GetNormal(i).Dot(triangle.GetTangent(i)) == Approx(0.0f));
		CHECK(triangle.GetNormal(i).Dot(triangle.GetBinormal(i)) == Approx(0.0f));
		CHECK(triangle.GetTangent(i).Dot(triangle.GetBinormal(i)) == Approx(0.0f));
	}
}

TEST_CASE("ManualTriangle_Bottom_Face_Flips_Only_The_Normal", "[manual_render_object]")
{
	// The underside of the water surface. Both faces share one UV parameterisation - u runs
	// along +X, v along +Z, derived from world position - so the tangent and binormal are the
	// SAME on both faces and only the normal flips.
	//
	// GetWorldNormal() computes n.x*T + n.y*B + n.z*N with no handedness sign, so what matters
	// is that a normal map perturbing +u tilts toward +X and +v tilts toward +Z. Keeping T and
	// B fixed satisfies that on both faces. Flipping the binormal as well would mirror the
	// ripple slopes on the underside - subtly wrong rather than obviously broken.
	ManualTriangleListOperation::Triangle top = MakeHorizontalTriangle();
	ManualTriangleListOperation::Triangle bottom = MakeHorizontalTriangle();

	for (uint8 i = 0; i < 3; ++i)
	{
		top.SetNormal(i, Vector3::UnitY);
		top.SetTangent(i, Vector3::UnitX);
		top.SetBinormal(i, Vector3::UnitZ);

		bottom.SetNormal(i, Vector3::NegativeUnitY);
		bottom.SetTangent(i, Vector3::UnitX);
		bottom.SetBinormal(i, Vector3::UnitZ);
	}

	for (uint8 i = 0; i < 3; ++i)
	{
		// Opposite normals.
		CHECK(top.GetNormal(i).Dot(bottom.GetNormal(i)) == Approx(-1.0f));

		// Identical UV-aligned tangent frame.
		CHECK(top.GetTangent(i) == bottom.GetTangent(i));
		CHECK(top.GetBinormal(i) == bottom.GetBinormal(i));

		// Each face's frame is orthogonal.
		CHECK(bottom.GetNormal(i).Dot(bottom.GetTangent(i)) == Approx(0.0f));
		CHECK(bottom.GetNormal(i).Dot(bottom.GetBinormal(i)) == Approx(0.0f));
		CHECK(bottom.GetTangent(i).Dot(bottom.GetBinormal(i)) == Approx(0.0f));
	}
}

TEST_CASE("ManualTriangle_Basis_Does_Not_Disturb_Colour_Or_Uv", "[manual_render_object]")
{
	// The water mesh carries its face identity in vertex colour alpha and its tiling in UVs;
	// adding the basis must leave both alone.
	ManualTriangleListOperation::Triangle triangle = MakeHorizontalTriangle();

	triangle.SetColor(0x80123456);
	triangle.SetUV(1, 0.25f, 0.75f);
	triangle.SetNormal(1, Vector3::NegativeUnitY);

	CHECK(triangle.GetColor(1) == 0x80123456);
	CHECK(triangle.GetUV(1)[0] == Approx(0.25f));
	CHECK(triangle.GetUV(1)[1] == Approx(0.75f));
}
