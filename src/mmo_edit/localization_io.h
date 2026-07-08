// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file localization_io.h
 *
 * @brief Project-wide export/import of localizable game-data strings.
 *
 * Export walks every localizable field across the whole proto::Project and writes a single
 * combined JSON translation file: each entry carries its type/id/field, the authoritative
 * English @c source string, and a @c translations map with a slot per supported target locale
 * (blank slots form the to-translate worklist). The file can be translated externally (by a
 * service or AI) and re-imported.
 *
 * Import reads that file back and writes the non-empty translations into each entry's
 * per-locale @c *_loc overrides. The English base fields are never modified; import is purely
 * additive/updatable. The caller is responsible for persisting the project afterwards.
 */

#pragma once

#include "base/typedefs.h"

namespace mmo::proto
{
	class Project;
}

namespace mmo
{
	/// Exports all localizable strings in the project to a combined JSON translation file.
	/// @param project The data project to read from.
	/// @param path Destination file path (.json).
	/// @param outError Receives a human-readable error message on failure.
	/// @param outEntryCount Receives the number of exported field entries on success.
	/// @return true on success, false on failure (outError is set).
	bool ExportTranslations(const proto::Project& project, const String& path, String& outError, size_t& outEntryCount);

	/// Imports translations from a combined JSON file, applying non-empty values to the
	/// matching entries' per-locale overrides. Base English fields are left untouched.
	/// @param project The data project to write into.
	/// @param path Source file path (.json).
	/// @param outError Receives a human-readable error message on failure.
	/// @param outAppliedCount Receives the number of applied translation values on success.
	/// @return true on success, false on failure (outError is set).
	bool ImportTranslations(proto::Project& project, const String& path, String& outError, size_t& outAppliedCount);
}
