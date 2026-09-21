// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "shared/client_data/proto_client/spell_visualizations.pb.h"

using namespace mmo;

TEST_CASE("Point light configs scatter into fog by default", "[spell_visualization]")
{
	const proto_client::PointLightConfig config;
	CHECK(config.fog_scattering() == Approx(1.0f));

	const auto* field = proto_client::PointLightConfig::descriptor()->FindFieldByName("fog_scattering");
	REQUIRE(field != nullptr);
	CHECK(field->number() == 9);
}
