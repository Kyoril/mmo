// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "bot_startup_selection.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace mmo
{
	namespace
	{
		std::string TrimCopy(std::string_view value)
		{
			size_t start = 0;
			while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0)
			{
				++start;
			}

			size_t end = value.size();
			while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
			{
				--end;
			}

			return std::string(value.substr(start, end - start));
		}

		std::string ToLowerCopy(std::string_view value)
		{
			std::string lowered;
			lowered.reserve(value.size());
			for (const char ch : value)
			{
				lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
			}
			return lowered;
		}

		bool EqualsIgnoreCase(std::string_view left, std::string_view right)
		{
			if (left.size() != right.size())
			{
				return false;
			}

			for (size_t i = 0; i < left.size(); ++i)
			{
				if (std::tolower(static_cast<unsigned char>(left[i])) != std::tolower(static_cast<unsigned char>(right[i])))
				{
					return false;
				}
			}

			return true;
		}

		std::string ChooseSelector(std::string_view cliSelector, std::string_view configSelector)
		{
			const std::string trimmedCli = TrimCopy(cliSelector);
			if (!trimmedCli.empty())
			{
				return trimmedCli;
			}

			return TrimCopy(configSelector);
		}
	}

	StartupProfileResolution ResolveStartupProfileSelection(
		std::string_view cliSelector,
		std::string_view configSelector,
		const std::vector<StartupProfileEntry>& availableProfiles)
	{
		StartupProfileResolution resolution;
		resolution.selector = ChooseSelector(cliSelector, configSelector);

		std::unordered_map<std::string, std::vector<size_t>> indicesByNormalizedKey;
		for (size_t i = 0; i < availableProfiles.size(); ++i)
		{
			const std::string normalizedKey = ToLowerCopy(TrimCopy(availableProfiles[i].key));
			if (normalizedKey.empty())
			{
				resolution.kind = StartupProfileResolutionKind::Invalid;
				resolution.candidateIndices.clear();
				return resolution;
			}

			indicesByNormalizedKey[normalizedKey].push_back(i);
		}

		for (const auto& [_, indices] : indicesByNormalizedKey)
		{
			if (indices.size() > 1)
			{
				resolution.kind = StartupProfileResolutionKind::Invalid;
				resolution.candidateIndices = indices;
				return resolution;
			}
		}

		if (!resolution.selector.empty())
		{
			std::vector<size_t> matches;
			for (size_t i = 0; i < availableProfiles.size(); ++i)
			{
				if (EqualsIgnoreCase(TrimCopy(availableProfiles[i].key), resolution.selector))
				{
					matches.push_back(i);
				}
			}

			if (matches.size() == 1)
			{
				resolution.kind = StartupProfileResolutionKind::Resolved;
				resolution.resolvedIndex = matches.front();
				resolution.resolvedKey = availableProfiles[matches.front()].key;
				return resolution;
			}

			resolution.kind = StartupProfileResolutionKind::Invalid;
			resolution.candidateIndices.resize(availableProfiles.size());
			for (size_t i = 0; i < availableProfiles.size(); ++i)
			{
				resolution.candidateIndices[i] = i;
			}
			return resolution;
		}

		if (availableProfiles.empty())
		{
			resolution.kind = StartupProfileResolutionKind::Invalid;
			return resolution;
		}

		if (availableProfiles.size() == 1)
		{
			resolution.kind = StartupProfileResolutionKind::Resolved;
			resolution.resolvedIndex = 0;
			resolution.resolvedKey = availableProfiles.front().key;
			return resolution;
		}

		resolution.kind = StartupProfileResolutionKind::NeedsPrompt;
		resolution.candidateIndices.resize(availableProfiles.size());
		for (size_t i = 0; i < availableProfiles.size(); ++i)
		{
			resolution.candidateIndices[i] = i;
		}
		return resolution;
	}

	StartupCharacterResolution ResolveStartupCharacterSelection(
		std::string_view cliSelector,
		std::string_view configSelector,
		bool createIfMissing,
		const std::vector<CharacterView>& availableCharacters)
	{
		StartupCharacterResolution resolution;
		resolution.selector = ChooseSelector(cliSelector, configSelector);

		std::unordered_map<std::string, std::vector<size_t>> indicesByNormalizedName;
		std::vector<size_t> malformedIndices;
		std::vector<size_t> duplicateIndices;
		for (size_t i = 0; i < availableCharacters.size(); ++i)
		{
			const std::string normalizedName = ToLowerCopy(TrimCopy(availableCharacters[i].GetName()));
			if (normalizedName.empty())
			{
				malformedIndices.push_back(i);
				continue;
			}

			indicesByNormalizedName[normalizedName].push_back(i);
		}

		for (const auto& [_, indices] : indicesByNormalizedName)
		{
			if (indices.size() > 1)
			{
				duplicateIndices.insert(duplicateIndices.end(), indices.begin(), indices.end());
			}
		}

		if (!malformedIndices.empty())
		{
			resolution.kind = StartupCharacterResolutionKind::NeedsPrompt;
			resolution.candidateIndices.resize(availableCharacters.size());
			for (size_t i = 0; i < availableCharacters.size(); ++i)
			{
				resolution.candidateIndices[i] = i;
			}
			return resolution;
		}

		if (!duplicateIndices.empty())
		{
			std::sort(duplicateIndices.begin(), duplicateIndices.end());
			resolution.kind = StartupCharacterResolutionKind::NeedsPrompt;
			resolution.candidateIndices = std::move(duplicateIndices);
			return resolution;
		}

		if (!resolution.selector.empty())
		{
			for (size_t i = 0; i < availableCharacters.size(); ++i)
			{
				const std::string name = TrimCopy(availableCharacters[i].GetName());
				if (!name.empty() && EqualsIgnoreCase(name, resolution.selector))
				{
					resolution.candidateIndices.push_back(i);
				}
			}

			if (resolution.candidateIndices.size() == 1)
			{
				const size_t index = resolution.candidateIndices.front();
				resolution.kind = StartupCharacterResolutionKind::Resolved;
				resolution.resolvedIndex = index;
				resolution.resolvedName = availableCharacters[index].GetName();
				resolution.candidateIndices.clear();
				return resolution;
			}

			if (resolution.candidateIndices.size() > 1)
			{
				resolution.kind = StartupCharacterResolutionKind::NeedsPrompt;
				return resolution;
			}

			resolution.kind = createIfMissing
				? StartupCharacterResolutionKind::CreateCharacter
				: StartupCharacterResolutionKind::NotFound;
			resolution.candidateIndices.resize(availableCharacters.size());
			for (size_t i = 0; i < availableCharacters.size(); ++i)
			{
				resolution.candidateIndices[i] = i;
			}
			return resolution;
		}

		if (availableCharacters.size() == 1)
		{
			resolution.kind = StartupCharacterResolutionKind::Resolved;
			resolution.resolvedIndex = 0;
			resolution.resolvedName = availableCharacters.front().GetName();
			return resolution;
		}

		resolution.kind = StartupCharacterResolutionKind::NeedsPrompt;
		resolution.candidateIndices.resize(availableCharacters.size());
		for (size_t i = 0; i < availableCharacters.size(); ++i)
		{
			resolution.candidateIndices[i] = i;
		}
		return resolution;
	}
}
