// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "environment_profile.h"

namespace mmo
{
	ColorCurve MakeEnvironmentCurve(const std::initializer_list<std::pair<float, Vector4>> keys)
	{
		ColorCurve curve;
		curve.Clear();

		for (const auto& [time, color] : keys)
		{
			curve.AddKey(time, color);
		}

		curve.CalculateTangents();
		return curve;
	}

	EnvironmentProfile EnvironmentProfile::MakeDefault()
	{
		EnvironmentProfile profile;

		// Seeded from Models/HorizonColor.hccv.
		profile.skyHorizon = MakeEnvironmentCurve({
			{ 0.1501f, Vector4(0.0076f, 0.0082f, 0.0120f, 1.0f) },
			{ 0.2468f, Vector4(0.4745f, 0.1373f, 0.1137f, 1.0f) },
			{ 0.2975f, Vector4(0.9964f, 0.6000f, 0.2000f, 1.0f) },
			{ 0.3762f, Vector4(0.9964f, 0.6039f, 0.3000f, 1.0f) },
			{ 0.4426f, Vector4(0.6980f, 0.9451f, 1.0000f, 1.0f) },
			{ 0.5074f, Vector4(0.4834f, 0.8898f, 1.0000f, 1.0f) },
			{ 0.8072f, Vector4(0.0464f, 0.1636f, 0.2796f, 1.0f) },
			{ 0.9688f, Vector4(0.0078f, 0.0078f, 0.0118f, 1.0f) },
		});

		// Seeded from Models/ZenithColor.hccv.
		profile.skyZenith = MakeEnvironmentCurve({
			{ 0.0000f, Vector4(0.0000f, 0.0000f, 0.0000f, 1.0f) },
			{ 0.0533f, Vector4(0.0000f, 0.0000f, 0.0000f, 1.0f) },
			{ 0.1882f, Vector4(0.0784f, 0.0627f, 0.0784f, 1.0f) },
			{ 0.2337f, Vector4(0.2235f, 0.0980f, 0.0941f, 1.0f) },
			{ 0.2856f, Vector4(0.2902f, 0.1412f, 0.1176f, 1.0f) },
			{ 0.3453f, Vector4(0.0980f, 0.1373f, 0.1804f, 1.0f) },
			{ 0.4321f, Vector4(0.0431f, 0.0863f, 0.2039f, 1.0f) },
			{ 0.7243f, Vector4(0.0235f, 0.0471f, 0.1216f, 1.0f) },
			{ 0.9893f, Vector4(0.0000f, 0.0000f, 0.0000f, 1.0f) },
		});

		// Seeded from Models/AmbientColor.hccv.
		profile.ambient = MakeEnvironmentCurve({
			{ 0.1029f, Vector4(0.0140f, 0.0140f, 0.0140f, 0.9973f) },
			{ 0.1707f, Vector4(0.0237f, 0.0163f, 0.0094f, 1.0000f) },
			{ 0.2159f, Vector4(0.0427f, 0.0345f, 0.0224f, 1.0000f) },
			{ 0.3617f, Vector4(0.0319f, 0.0360f, 0.0427f, 0.9973f) },
			{ 0.5003f, Vector4(0.0293f, 0.0412f, 0.0600f, 0.9990f) },
			{ 1.0000f, Vector4(0.0157f, 0.0157f, 0.0157f, 0.9961f) },
		});

		// Seeded from Models/CloudColor.hccv.
		profile.clouds = MakeEnvironmentCurve({
			{ 0.1356f, Vector4(0.0550f, 0.0581f, 0.0853f, 1.0f) },
			{ 0.2046f, Vector4(0.6351f, 0.2746f, 0.0632f, 1.0f) },
			{ 0.5003f, Vector4(0.9479f, 0.9479f, 0.9479f, 1.0f) },
			{ 1.0000f, Vector4(0.0549f, 0.0588f, 0.0863f, 1.0f) },
		});

		// Formerly hardcoded in SkyComponent::UpdateLighting. SkyComponent still blends sun and
		// moon by the sun's height.
		profile.sun = MakeEnvironmentCurve({ { 0.0f, Vector4(1.0f, 0.95f, 0.9f, 1.0f) }, { 1.0f, Vector4(1.0f, 0.95f, 0.9f, 1.0f) } });
		profile.moon = MakeEnvironmentCurve({ { 0.0f, Vector4(0.3f, 0.4f, 0.65f, 0.12f) }, { 1.0f, Vector4(0.3f, 0.4f, 0.65f, 0.12f) } });

		// rgb = fog ambient radiance, a = density multiplier.
		profile.fog = MakeEnvironmentCurve({
			{ 0.0f, Vector4(0.02f, 0.04f, 0.08f, 1.1f) },   // Night
			{ 0.2f, Vector4(0.05f, 0.06f, 0.1f, 1.2f) },    // Pre-dawn
			{ 0.27f, Vector4(0.85f, 0.6f, 0.45f, 1.5f) },   // Dawn
			{ 0.5f, Vector4(0.55f, 0.7f, 0.9f, 1.0f) },     // Midday
			{ 0.73f, Vector4(0.85f, 0.55f, 0.4f, 1.3f) },   // Dusk
			{ 0.8f, Vector4(0.05f, 0.06f, 0.1f, 1.15f) },   // After dusk
			{ 1.0f, Vector4(0.02f, 0.04f, 0.08f, 1.1f) },   // Night
		});

		// rgb = sun colour inside the fog, a = shaft multiplier. Low-sun keys are strongly saturated
		// on purpose: the shaft core is bright enough for ACES to push it toward white, so a pale
		// tint would read as plain white light.
		profile.sunScatter = MakeEnvironmentCurve({
			{ 0.0f, Vector4(0.3f, 0.4f, 0.65f, 0.3f) },     // Night (moon)
			{ 0.2f, Vector4(0.3f, 0.4f, 0.65f, 0.3f) },     // Pre-dawn (moon)
			{ 0.23f, Vector4(1.0f, 0.42f, 0.18f, 0.8f) },   // Sunrise
			{ 0.3f, Vector4(1.0f, 0.62f, 0.32f, 1.0f) },    // Morning
			{ 0.5f, Vector4(1.0f, 0.93f, 0.8f, 0.35f) },    // Midday
			{ 0.7f, Vector4(1.0f, 0.58f, 0.28f, 1.0f) },    // Evening
			{ 0.77f, Vector4(1.0f, 0.4f, 0.16f, 0.8f) },    // Sunset
			{ 0.8f, Vector4(0.3f, 0.4f, 0.65f, 0.3f) },     // After dusk (moon)
			{ 1.0f, Vector4(0.3f, 0.4f, 0.65f, 0.3f) },     // Night (moon)
		});

		return profile;
	}

	const std::shared_ptr<const EnvironmentProfile>& EnvironmentProfile::GetDefault()
	{
		static const std::shared_ptr<const EnvironmentProfile> s_default = std::make_shared<const EnvironmentProfile>(MakeDefault());
		return s_default;
	}
}
