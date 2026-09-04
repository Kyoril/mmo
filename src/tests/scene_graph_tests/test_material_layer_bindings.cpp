// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <sstream>

#include "binary_io/stream_sink.h"
#include "binary_io/stream_source.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"
#include "graphics/material.h"
#include "graphics/material_instance.h"
#include "scene_graph/material_serializer.h"

using namespace mmo;

// Terrain materials name their per-layer shader parameters differently in every material,
// so the material declares a binding table saying which parameter drives which layer. These
// tests pin the serialization contract around it, and in particular the rule that a material
// which does not use the feature must keep writing v0.7 byte-identical - that is what lets
// the shipped materials be re-saved without churn.

namespace
{
	std::string Serialize(const Material& material)
	{
		std::ostringstream stream;
		io::StreamSink sink{ stream };
		io::Writer writer{ sink };
		MaterialSerializer serializer{};
		serializer.Export(material, writer);
		return stream.str();
	}

	bool Deserialize(const std::string& bytes, Material& outMaterial)
	{
		std::istringstream stream{ bytes };
		io::StreamSource source{ stream };
		io::Reader reader{ source };
		MaterialDeserializer deserializer{ outMaterial };
		return deserializer.Read(reader);
	}

	MaterialLayerBinding MakeBinding(const char* prefix)
	{
		MaterialLayerBinding binding;
		binding.displayName = String(prefix) + " Display";
		binding.albedoTextureParam = String(prefix) + "_BaseColor";
		binding.normalTextureParam = String(prefix) + "_Normal";
		binding.scaleScalarParam = String("scale_textures_") + prefix;
		binding.heightTextureParam = String(prefix) + "_Height";
		binding.heightScaleParam = String("HeightScale_") + prefix;
		binding.heightOffsetParam = String("HeightOffset_") + prefix;
		return binding;
	}

	void RequireEqual(const MaterialLayerBinding& a, const MaterialLayerBinding& b)
	{
		REQUIRE(a.displayName == b.displayName);
		REQUIRE(a.albedoTextureParam == b.albedoTextureParam);
		REQUIRE(a.normalTextureParam == b.normalTextureParam);
		REQUIRE(a.scaleScalarParam == b.scaleScalarParam);
		REQUIRE(a.heightTextureParam == b.heightTextureParam);
		REQUIRE(a.heightScaleParam == b.heightScaleParam);
		REQUIRE(a.heightOffsetParam == b.heightOffsetParam);
	}

	// The file version lives in the first chunk; find it rather than assuming an offset.
	uint16 ReadFileVersion(const std::string& bytes)
	{
		REQUIRE(bytes.size() >= 12);
		uint32 version = 0;
		std::memcpy(&version, bytes.data() + 8, sizeof(version));
		return static_cast<uint16>(version);
	}
}

TEST_CASE("Layer bindings round-trip through the material serializer", "[material_layer_binding]")
{
	Material source{ "Test" };
	source.SetLayerBinding(0, MakeBinding("Grass"));
	source.SetLayerBinding(2, MakeBinding("Dirt"));
	source.SetLayerBlendSharpnessParam("height_blend_sharpness");

	Material loaded{ "Loaded" };
	REQUIRE(Deserialize(Serialize(source), loaded));

	RequireEqual(loaded.GetLayerBinding(0), MakeBinding("Grass"));
	RequireEqual(loaded.GetLayerBinding(2), MakeBinding("Dirt"));

	// Layers left unset must survive as empty rather than picking up a neighbour's names.
	REQUIRE(loaded.GetLayerBinding(1).IsEmpty());
	REQUIRE(loaded.GetLayerBinding(3).IsEmpty());
	REQUIRE(loaded.GetLayerBlendSharpnessParam() == "height_blend_sharpness");
}

TEST_CASE("A material without layer bindings still writes v0.7 and no MBND chunk", "[material_layer_binding]")
{
	Material material{ "Test" };
	const std::string bytes = Serialize(material);

	REQUIRE(ReadFileVersion(bytes) == material_version::Version_0_7);
	REQUIRE(bytes.find("MBND") == std::string::npos);
}

TEST_CASE("Declaring any layer binding promotes the file to v0.8", "[material_layer_binding]")
{
	Material material{ "Test" };
	MaterialLayerBinding binding;
	binding.scaleScalarParam = "Tiling01";
	material.SetLayerBinding(1, binding);

	REQUIRE(ReadFileVersion(Serialize(material)) == material_version::Version_0_8);
}

TEST_CASE("Declaring only the sharpness parameter promotes the file to v0.8", "[material_layer_binding]")
{
	Material material{ "Test" };
	material.SetLayerBlendSharpnessParam("height_blend_sharpness");

	REQUIRE(ReadFileVersion(Serialize(material)) == material_version::Version_0_8);
}

TEST_CASE("Layer bindings survive a second round-trip unchanged", "[material_layer_binding]")
{
	Material source{ "Test" };
	source.SetLayerBinding(3, MakeBinding("Road"));
	source.SetLayerBlendSharpnessParam("sharpness");

	const std::string first = Serialize(source);

	Material loaded{ "Loaded" };
	REQUIRE(Deserialize(first, loaded));

	// Re-serializing what we read back must produce the same bytes, or repeated editor saves
	// would churn the asset.
	REQUIRE(Serialize(loaded) == first);
}

TEST_CASE("A material instance forwards layer bindings to its parent", "[material_layer_binding]")
{
	const auto parent = std::make_shared<Material>("Parent");
	parent->SetLayerBinding(0, MakeBinding("Grass"));
	parent->SetLayerBlendSharpnessParam("height_blend_sharpness");

	MaterialInstance instance{ "Instance", parent };

	RequireEqual(instance.GetLayerBinding(0), MakeBinding("Grass"));
	REQUIRE(instance.GetLayerBlendSharpnessParam() == "height_blend_sharpness");

	// Layer bindings are intentionally not overridable per instance.
	REQUIRE(instance.GetLayerBinding(1).IsEmpty());
}

// The whole per-layer binding design rests on one property of MaterialInstance: parameters
// are resolved by NAME, not by position. That is what lets a .hmat gain new scalars - the
// eight height blend parameters, say - without every .hmi that overrides a subset of the old
// ones having to be re-saved in lockstep. The shipped Oakenshire_BoarTerrain.hmi already
// relies on it, carrying 27 scalars against a parent's 29, in a different order.
TEST_CASE("Material instance parameters resolve by name, not by position", "[material_layer_binding]")
{
	const auto parent = std::make_shared<Material>("Parent");
	parent->AddScalarParameter("A", 1.0f);
	parent->AddScalarParameter("B", 2.0f);
	parent->AddScalarParameter("C", 3.0f);

	MaterialInstance instance{ "Instance", parent };

	// Override out of order, and only a subset, the way a stale .hmi does.
	instance.SetScalarParameter("C", 30.0f);
	instance.SetScalarParameter("A", 10.0f);

	const auto& params = instance.GetScalarParameters();
	REQUIRE(params.size() == 3);

	// Order follows the parent's declaration order, which is what the constant buffer layout
	// is built from - an override must not reorder or reseat anything.
	REQUIRE(params[0].name == "A");
	REQUIRE(params[1].name == "B");
	REQUIRE(params[2].name == "C");

	REQUIRE(params[0].value == Approx(10.0f));
	REQUIRE(params[1].value == Approx(2.0f));
	REQUIRE(params[2].value == Approx(30.0f));
}

TEST_CASE("A material instance cannot introduce parameters its parent lacks", "[material_layer_binding]")
{
	const auto parent = std::make_shared<Material>("Parent");
	parent->AddScalarParameter("A", 1.0f);

	MaterialInstance instance{ "Instance", parent };
	instance.AddScalarParameter("NotInParent", 5.0f);

	// AddScalarParameter is a deliberate no-op on instances. This is why layer bindings are
	// not overridable per instance: a binding could only ever name a parent parameter.
	REQUIRE(instance.GetScalarParameters().size() == 1);
	REQUIRE(instance.GetScalarParameters()[0].name == "A");
}
