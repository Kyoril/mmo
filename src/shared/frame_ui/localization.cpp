// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "localization.h"

#include "assets/asset_registry.h"
#include "simple_file_format/sff_read_tree.h"
#include "simple_file_format/sff_load_file.h"
#include "log/default_log_levels.h"

#include "luabind/luabind.hpp"


namespace mmo
{
	Localization::Localization()
	{
	}

	const std::string * Localization::FindStringById(const std::string & id) const
	{
		auto it = m_translationsById.find(id);
		if (it != m_translationsById.end())
		{
			return &it->second;
		}

		return nullptr;
	}

	bool Localization::LoadFromFile()
	{
		typedef std::string::const_iterator Iterator;

		sff::read::tree::Table<Iterator> fileTable;
		std::string content;

		// Load file
		auto fileStrm = AssetRegistry::OpenFile("Localization.txt");
		if (!fileStrm)
		{
			ELOG("Could not find file Localization.txt!");
			return false;
		}

		try
		{
			sff::loadTableFromFile(fileTable, content, *fileStrm);
		}
		catch (const sff::read::ParseException<Iterator> &e)
		{
			const auto line = sff::read::Scanner<Iterator>::getCharacterLine(
				e.position.begin,
				content.begin());

			ELOG("Error in file Localization.txt in line " << line + 1 << ": " << e.what());
			return false;
		}

		// Load strings table
		if (const sff::read::tree::Array<Iterator> *const array = fileTable.getArray("strings"))
		{
			const size_t count = array->getSize();
			for (size_t i = 0; i < count; ++i)
			{
				const sff::read::tree::Table<Iterator> *const table =
					array->getTable(i);

				if (!table)
				{
					ELOG("Non-Table array element found");
					continue;
				}

				const std::string keyString = table->getString("key");
				const std::string valueString = table->getString("string");

				m_translationsById[keyString] = valueString;
			}
		}

		return true;
	}

	void Localization::AddToLuaScript(lua_State* state)
	{
		luabind::object globals = luabind::globals(state);

		for (auto& kv : m_translationsById)
		{
			globals[kv.first] = kv.second;
		}
	}
}
