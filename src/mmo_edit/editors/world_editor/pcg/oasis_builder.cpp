// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "oasis_builder.h"

#include <chrono>
#include <memory>

namespace mmo
{
	namespace
	{
		/// Generator label in the names of the map entities an oasis creates.
		const String OasisLabel = "Oasis";

		const char* DescribeError(const pcg::OasisError error)
		{
			switch (error)
			{
			case pcg::OasisError::None:
				return "no error";
			case pcg::OasisError::TooFewPoints:
				return "the loop needs at least 3 points";
			case pcg::OasisError::ZeroArea:
				return "the loop encloses no area";
			case pcg::OasisError::SelfIntersecting:
				return "the loop intersects itself";
			}

			return "unknown error";
		}
	}

	OasisBuildReport RebuildOasis(IWorldEditor& editor, pcg::OasisRecord& record, const pcg::OasisAssets& assets)
	{
		const auto start = std::chrono::steady_clock::now();

		// Validate first. Validation (and the influence) does not depend on the heights, so any
		// height function will do; a broken loop must leave the world untouched.
		pcg::OasisShape probe;
		const pcg::OasisError validation = pcg::PrepareOasis(record.loop, record.seed, record.params,
			[](float, float) { return 0.0f; }, probe);
		if (validation != pcg::OasisError::None)
		{
			OasisBuildReport report;
			report.error = String("Invalid oasis loop: ") + DescribeError(validation);
			report.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			return report;
		}

		// The shape is prepared on the ground as it is once the old contribution is undone; the
		// terrain and placement functions read it from there.
		const auto shape = std::make_shared<pcg::OasisShape>();

		GeneratedContent content;
		content.influence = pcg::GetInfluenceRect(probe);
		content.tileMaterial = assets.terrainMaterial;
		content.waterMaterial = assets.waterMaterial;
		content.prepare = [&record, shape](const GroundFunction& ground, String& error)
		{
			const pcg::OasisError prepared = pcg::PrepareOasis(record.loop, record.seed, record.params, ground, *shape);
			if (prepared != pcg::OasisError::None)
			{
				// Cannot happen (validation does not depend on heights), but never apply a bad shape.
				error = String("Invalid oasis loop: ") + DescribeError(prepared);
				return false;
			}
			return true;
		};
		content.terrain = [shape](const float x, const float z, const float baseHeight, pcg::terrain_channel::Type)
		{
			return pcg::EvaluateTerrain(*shape, x, z, baseHeight);
		};
		content.placements = [shape, &assets](const GroundFunction& ground, const GroundFunction& slope, std::vector<pcg::Placement>& out)
		{
			pcg::BuildPlacements(*shape, assets, ground, slope, out);
		};

		OasisBuildReport report = ApplyGeneratedContent(editor, content, record.content, record.id, OasisLabel);
		report.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		return report;
	}

	OasisBuildReport DeleteOasisContent(IWorldEditor& editor, pcg::OasisRecord& record)
	{
		return RemoveGeneratedContent(editor, record.content);
	}
}
