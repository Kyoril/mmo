// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#ifdef _WIN32

#include "graphics_d3d11/material_compiler_d3d11.h"

using namespace mmo;

// The material compiler decides the result type of a binary expression from its operands,
// but it does NOT do so uniformly: AddMultiply (and now AddMax/AddMin) widen to the wider
// operand, while AddAddition, AddSubtract and AddDivide take the type of the FIRST operand
// alone. Getting that wrong emits `float expr_N = <float4 expression>;`, which silently
// truncates to .x - no compiler error, no editor warning, just a plausible-looking wrong
// image. These tests pin both behaviours so the asymmetry is documented in code rather
// than rediscovered in a shader.

namespace
{
	struct Operands
	{
		MaterialCompilerD3D11 compiler;
		ExpressionIndex scalar = compiler.AddExpression("1.0", ExpressionType::Float_1);
		ExpressionIndex vector = compiler.AddExpression("float4(1,1,1,1)", ExpressionType::Float_4);
	};
}

TEST_CASE("AddMax widens to the wider operand", "[material_compiler]")
{
	Operands ops;

	REQUIRE(ops.compiler.GetExpressionType(ops.compiler.AddMax(ops.scalar, ops.vector)) == ExpressionType::Float_4);
	REQUIRE(ops.compiler.GetExpressionType(ops.compiler.AddMax(ops.vector, ops.scalar)) == ExpressionType::Float_4);
	REQUIRE(ops.compiler.GetExpressionType(ops.compiler.AddMax(ops.scalar, ops.scalar)) == ExpressionType::Float_1);
}

TEST_CASE("AddMin widens to the wider operand", "[material_compiler]")
{
	Operands ops;

	REQUIRE(ops.compiler.GetExpressionType(ops.compiler.AddMin(ops.scalar, ops.vector)) == ExpressionType::Float_4);
	REQUIRE(ops.compiler.GetExpressionType(ops.compiler.AddMin(ops.vector, ops.scalar)) == ExpressionType::Float_4);
}

TEST_CASE("AddMax and AddMin reject missing operands", "[material_compiler]")
{
	Operands ops;

	REQUIRE(ops.compiler.AddMax(IndexNone, ops.vector) == IndexNone);
	REQUIRE(ops.compiler.AddMax(ops.vector, IndexNone) == IndexNone);
	REQUIRE(ops.compiler.AddMin(IndexNone, ops.vector) == IndexNone);
	REQUIRE(ops.compiler.AddMin(ops.vector, IndexNone) == IndexNone);
}

TEST_CASE("Additive intrinsics take the type of their first operand only", "[material_compiler]")
{
	Operands ops;

	// Characterisation, not endorsement: this is the trap that any new graph wiring has to
	// be arranged around. Put the float4 first, or the expression truncates to a float.
	REQUIRE(ops.compiler.GetExpressionType(ops.compiler.AddSubtract(ops.scalar, ops.vector)) == ExpressionType::Float_1);
	REQUIRE(ops.compiler.GetExpressionType(ops.compiler.AddSubtract(ops.vector, ops.scalar)) == ExpressionType::Float_4);
	REQUIRE(ops.compiler.GetExpressionType(ops.compiler.AddAddition(ops.scalar, ops.vector)) == ExpressionType::Float_1);
	REQUIRE(ops.compiler.GetExpressionType(ops.compiler.AddDivide(ops.scalar, ops.vector)) == ExpressionType::Float_1);

	// AddMultiply is the one binary intrinsic that already widened; Max/Min follow it.
	REQUIRE(ops.compiler.GetExpressionType(ops.compiler.AddMultiply(ops.scalar, ops.vector)) == ExpressionType::Float_4);
}

#endif
