// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "particle_params_cache.h"

#include "assets/asset_registry.h"
#include "binary_io/reader.h"
#include "binary_io/stream_source.h"
#include "scene_graph/particle_emitter.h"
#include "scene_graph/particle_emitter_serializer.h"

#include <map>
#include <memory>

namespace mmo
{
	const ParticleSystemParameters* LoadCachedParticleSystem(const String& file)
	{
		static std::map<String, std::unique_ptr<ParticleSystemParameters>> s_cache;

		const auto it = s_cache.find(file);
		if (it != s_cache.end())
		{
			return it->second.get();
		}

		std::unique_ptr<ParticleSystemParameters> params;
		if (const auto stream = AssetRegistry::OpenFile(file))
		{
			io::StreamSource source(*stream);
			io::Reader reader(source);
			ParticleSystemSerializer serializer;
			auto loaded = std::make_unique<ParticleSystemParameters>();
			if (serializer.Deserialize(*loaded, reader))
			{
				params = std::move(loaded);
			}
		}

		const ParticleSystemParameters* result = params.get();
		s_cache.emplace(file, std::move(params));
		return result;
	}
}
