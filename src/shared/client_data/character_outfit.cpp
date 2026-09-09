// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "character_outfit.h"

#include "shared/client_data/proto_client/classes.pb.h"

namespace mmo
{
	namespace proto_client
	{
		namespace
		{
			/// The wildcard value of the race and gender fields.
			constexpr int32 anyValue = -1;
		}

		const CharacterOutfit* SelectCharacterOutfit(const ClassEntry& classEntry, const int32 race, const int32 gender)
		{
			const CharacterOutfit* best = nullptr;
			int32 bestScore = -1;

			for (const CharacterOutfit& outfit : classEntry.outfits())
			{
				if (outfit.race() != anyValue && outfit.race() != race)
				{
					continue;
				}

				if (outfit.gender() != anyValue && outfit.gender() != gender)
				{
					continue;
				}

				// The more specific entry wins. A race match is worth more than a gender match so
				// that a race specific outfit beats a gender specific one.
				int32 score = 0;
				if (outfit.race() != anyValue)
				{
					score += 2;
				}

				if (outfit.gender() != anyValue)
				{
					score += 1;
				}

				// Strictly greater keeps the first entry of equally specific candidates.
				if (score > bestScore)
				{
					bestScore = score;
					best = &outfit;
				}
			}

			return best;
		}
	}
}
