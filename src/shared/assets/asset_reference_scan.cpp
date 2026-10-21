// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "asset_reference_scan.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>


namespace mmo
{
	namespace
	{
		/// Extensions of the assets a file can reference, without the dot.
		constexpr std::array<std::string_view, 8> ReferencedExtensions = {
			"hmsh", "hmat", "hmi", "htex", "hwmo", "hkit", "hmf", "skel"
		};

		/// Extensions of the formats that reference other assets, without the dot.
		constexpr std::array<std::string_view, 9> ReferencingExtensions = {
			"wobj", "hwmo", "hkit", "hfol", "tile", "hmsh", "hmat", "hmi", "hmf"
		};

		bool IsPathCharacter(const char c)
		{
			return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.' || c == '/' || c == '\\' || c == ' ';
		}

		bool EqualsIgnoreCase(const std::string_view a, const std::string_view b)
		{
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const char x, const char y)
			{
				return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
			});
		}

		/// Returns the length of the referenced extension starting at offset (just behind a dot), or 0.
		size_t MatchExtension(const std::string_view content, const size_t offset)
		{
			for (const std::string_view extension : ReferencedExtensions)
			{
				if (offset + extension.size() <= content.size() && EqualsIgnoreCase(content.substr(offset, extension.size()), extension))
				{
					return extension.size();
				}
			}

			return 0;
		}
	}

	bool CanReferenceAssets(const std::string_view filename)
	{
		const size_t dot = filename.find_last_of('.');
		if (dot == std::string_view::npos)
		{
			return false;
		}

		const std::string_view extension = filename.substr(dot + 1);
		return std::any_of(ReferencingExtensions.begin(), ReferencingExtensions.end(), [extension](const std::string_view candidate)
		{
			return EqualsIgnoreCase(extension, candidate);
		});
	}

	void ScanAssetReferences(const std::string_view content, const std::function<bool(const String&)>& exists, std::vector<String>& outReferences)
	{
		std::set<String> found;

		for (size_t dot = content.find('.'); dot != std::string_view::npos; dot = content.find('.', dot + 1))
		{
			const size_t extensionLength = MatchExtension(content, dot + 1);
			if (extensionLength == 0)
			{
				continue;
			}

			size_t start = dot;
			while (start > 0 && IsPathCharacter(content[start - 1]))
			{
				--start;
			}

			if (start == dot)
			{
				continue;
			}

			String candidate(content.substr(start, dot + 1 + extensionLength - start));
			std::replace(candidate.begin(), candidate.end(), '\\', '/');

			// Drop leading characters until a spelling exists; the longest existing one is the reference.
			const size_t nameEnd = dot - start;
			for (size_t skip = 0; skip < nameEnd; ++skip)
			{
				if (candidate[skip] == ' ' || candidate[skip] == '/')
				{
					continue;
				}

				String name = candidate.substr(skip);
				if (exists(name))
				{
					if (found.insert(name).second)
					{
						outReferences.push_back(std::move(name));
					}
					break;
				}
			}
		}
	}
}
