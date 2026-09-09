// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	namespace proto_client
	{
		class ClassEntry;
		class CharacterOutfit;

		/// Picks the character creation outfit of a class which best matches a race and gender.
		///	Entries whose race or gender is set to something other than the wildcard -1 and does
		///	not match are skipped. Of the remaining entries the most specific one wins: an
		///	explicit race counts double an explicit gender, and the first entry in list order
		///	breaks ties.
		/// @param classEntry The class whose outfit list is searched.
		/// @param race The selected race id.
		/// @param gender The selected gender (0 = male, 1 = female).
		/// @return The best matching outfit, or nullptr when the class defines none that apply.
		const CharacterOutfit* SelectCharacterOutfit(const ClassEntry& classEntry, int32 race, int32 gender);
	}
}
