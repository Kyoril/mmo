// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "emote_utils.h"

#include "shared/proto_data/emotes.pb.h"

namespace mmo
{
	namespace
	{
		/// Replaces every occurrence of a placeholder in an emote template.
		void replaceEmoteToken(String& text, const String& token, const String& value)
		{
			size_t pos = 0;
			while ((pos = text.find(token, pos)) != String::npos)
			{
				text.replace(pos, token.size(), value);
				pos += value.size();
			}
		}
	}

	String ComposeEmoteText(const proto::EmoteEntry& emote, const LocaleIndex locale, const String& sourceName, const String& targetName, const bool hasTarget, const bool isSelf)
	{
		// Pick the best matching template for the target situation, falling back to less
		// specific templates when the preferred one is not authored.
		const String* base = nullptr;
		const google::protobuf::RepeatedPtrField<proto::LocalizedString>* loc = nullptr;

		if (isSelf && !emote.textself().empty())
		{
			base = &emote.textself();
			loc = &emote.textself_loc();
		}
		else if (hasTarget && !emote.texttarget().empty())
		{
			base = &emote.texttarget();
			loc = &emote.texttarget_loc();
		}
		else if (!emote.textnotarget().empty())
		{
			base = &emote.textnotarget();
			loc = &emote.textnotarget_loc();
		}

		// Emotes without any chat template are animation-only.
		if (!base)
		{
			return {};
		}

		String text = GetLocalizedString(*base, *loc, locale);
		replaceEmoteToken(text, "%s", sourceName);
		replaceEmoteToken(text, "%t", targetName);
		return text;
	}

	uint32 SelectNextPoseVariant(const std::vector<std::pair<uint32, uint32>>& sortedVariants, const uint32 current)
	{
		if (sortedVariants.empty())
		{
			return 0;
		}

		if (current == 0)
		{
			return sortedVariants.front().second;
		}

		for (size_t i = 0; i < sortedVariants.size(); ++i)
		{
			if (sortedVariants[i].second == current)
			{
				return (i + 1 < sortedVariants.size()) ? sortedVariants[i + 1].second : 0;
			}
		}

		// Stale selection (variant no longer available): reset to the default pose.
		return 0;
	}
}
