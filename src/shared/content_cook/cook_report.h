// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <ostream>
#include <vector>

#include "base/typedefs.h"

namespace mmo
{
	/// @brief One problem the cook found. Cooking warns and goes on; nothing a warning names stops a release.
	struct CookWarning
	{
		/// @brief Asset path relative to the data directory.
		String asset;

		/// @brief What went wrong.
		String message;

		[[nodiscard]] bool operator<(const CookWarning& other) const
		{
			return asset != other.asset ? asset < other.asset : message < other.message;
		}

		[[nodiscard]] bool operator==(const CookWarning& other) const
		{
			return asset == other.asset && message == other.message;
		}
	};

	/// @brief Collects the warnings of a cook run.
	class CookReport final
	{
	public:
		/// @brief Records a warning and logs it.
		/// @param asset Asset path relative to the data directory.
		/// @param message What went wrong.
		void Warn(String asset, String message);

		/// @brief Adds warnings recorded elsewhere (another platform's pack) without logging them again.
		/// @param warnings The warnings.
		void Merge(const std::vector<CookWarning>& warnings);

		/// @brief Gets the warnings in the order they were recorded.
		[[nodiscard]] const std::vector<CookWarning>& GetWarnings() const { return m_warnings; }

		/// @brief Gets the warnings sorted by asset and message, without duplicates.
		[[nodiscard]] std::vector<CookWarning> GetSortedWarnings() const;

		/// @brief Writes one GitHub Actions annotation per warning ("::warning title=cook::<asset>: <message>").
		/// @param out The stream to write to (stdout in CI).
		void WriteGithubAnnotations(std::ostream& out) const;

	private:
		std::vector<CookWarning> m_warnings;
	};
}
