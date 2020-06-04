
#pragma once

#include "base/typedefs.h"
#include "base/filesystem.h"
#include "log/default_log_levels.h"

#include <functional>
#include <fstream>

namespace mmo
{
	struct ProjectSaver
	{
		struct Manager
		{
			typedef std::function<bool(const String &)> SaveManager;
			typedef std::function<void(const String &)> HashManager;

			String fileName;
			String name;
			SaveManager save;
			HashManager hash;
				
			Manager()
			{
			}

			template<class T>
			Manager(const String &name,
				    const String &fileName,
				    T &manager)
				: fileName(fileName)
				, name(name)
				, save([this, name, &manager](
					        const String & fileName) mutable -> bool
			{
				return this->saveManagerToFile(fileName, name, manager);
			})
				, hash([this, &manager](const String &hashString)
			{
				manager.hashString = hashString;
			})
			{
			}

		private:

			template<class T>
			static bool saveManagerToFile(
				const String &filename,
				const String &name,
				T &manager)
			{
				std::ofstream file(filename.c_str(), std::ios::out | std::ios::binary);
				if (!file)
				{
					ELOG("Could not save file '" << filename << "'");
					return false;
				}

				return manager.save(file);
			}
		};

		typedef std::vector<Manager> Managers;

		static bool save(const std::filesystem::path &directory, Managers &managers);
	};
}
