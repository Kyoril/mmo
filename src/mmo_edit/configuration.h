// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "simple_file_format/sff_write_table.h"
#include "base/typedefs.h"

namespace mmo
{
	/// Manages the login server configuration.
	class Configuration
	{
	public:
		/// Config file version: used to detect new configuration files
		static const uint32 ModelEditorConfigVersion;

	public:
		/**
		 * \brief Path to the asset registry used to store assets.
		 */
		String assetRegistryPath;
		
		/// @brief Path to the editor project.
		String projectPath;


	public:
		/**
		 * \brief Initializes a new instance of the Configuration class.
		 */
		explicit Configuration();

	public:

		/**
		 * \brief Loads the configuration settings from a file.
		 * \param fileName Name of the configuration file.
		 * \return true on success, false on failure.
		 */
		bool Load(const String& fileName);

		/**
		 * \brief Saves the configuration settings into a file.
		 * \param fileName Name of the configuration file.
		 * \return true on success, false on failure.
		 */
		bool Save(const String& fileName);
	};
}
