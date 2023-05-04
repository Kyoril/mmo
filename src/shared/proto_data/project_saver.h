#pragma once

#include <fstream>

#include "base/typedefs.h"
#include "log/default_log_levels.h"

namespace mmo
{
	namespace proto
	{
		struct ProjectSaver
		{
			struct Manager
			{
				typedef std::function<bool(const mmo::String &)> SaveManager;
				typedef std::function<void(const mmo::String &)> HashManager;

				mmo::String fileName;
				mmo::String name;
				SaveManager save;
				HashManager hash;
				
				Manager()
				{
				}

				template<class T>
				Manager(const mmo::String &name,
				        const mmo::String &fileName,
				        T &manager)
					: fileName(fileName)
					, name(name)
					, save([this, name, &manager](
					           const mmo::String & fileName) mutable -> bool
				{
					return this->saveManagerToFile(fileName, name, manager);
				})
					, hash([this, &manager](const mmo::String &hashString)
				{
					manager.hashString = hashString;
				})
				{
				}

			private:

				template<class T>
				static bool saveManagerToFile(
				    const mmo::String &filename,
				    const mmo::String &name,
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
}
