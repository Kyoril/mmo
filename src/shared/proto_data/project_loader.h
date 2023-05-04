#pragma once

#include "base/typedefs.h"
#include "virtual_dir/file_system_reader.h"
#include "simple_file_format/sff_read_tree.h"
#include "simple_file_format/sff_load_file.h"
#include "log/default_log_levels.h"
#include "google/protobuf/io/coded_stream.h"

namespace mmo
{
	namespace proto
	{
		/// Context for several functionalities during the load process.
		struct DataLoadContext
		{
		private:

			DataLoadContext(const DataLoadContext &Other) = delete;
			DataLoadContext &operator=(const DataLoadContext &Other) = delete;

		public:

			typedef std::function<void(const mmo::String &)> OnError;
			typedef std::function<bool()> LoadLaterFunction;
			typedef std::vector<LoadLaterFunction> LoadLater;

			OnError onError;
			OnError onWarning;
			LoadLater loadLater;
			uint32 version;

			DataLoadContext()
			{
			}
			virtual ~DataLoadContext()
			{

			}
			bool executeLoadLater()
			{
				bool success = true;

				for (const LoadLaterFunction &function : loadLater)
				{
					if (!function())
					{
						success = false;
					}
				}

				return success;
			}
		};

		template <class Context>
		struct ProjectLoader
		{
			struct ManagerEntry
			{
				typedef std::function < bool(std::istream &file, const mmo::String &fileName, Context &context) > LoadFunction;

				mmo::String name;
				LoadFunction load;

				template<typename T>
				ManagerEntry(
				    const mmo::String &name,
				    T &manager
				)
					: name(name)
					, load([this, name, &manager](
					           std::istream & file,
					           const mmo::String & fileName,
					           Context & context) mutable -> bool
				{
					return this->loadManagerFromFile(file, fileName, context, manager, name);
				})
				{
				}

			private:

				template<typename T>
				static bool loadManagerFromFile(
				    std::istream &file,
				    const mmo::String &fileName,
				    Context &context,
				    T &manager,
				    const mmo::String &arrayName)
				{
					return manager.load(file);
				}
			};

			typedef std::vector<ManagerEntry> Managers;

			typedef mmo::String::const_iterator StringIterator;

			static bool load(virtual_dir::IReader &directory, const Managers &managers, Context &context)
			{
				const virtual_dir::Path projectFilePath = "project.txt";
				const auto projectFile = directory.readFile(projectFilePath, false);
				if (!projectFile)
				{
					ELOG("Could not open project file '" << projectFilePath << "'");
					return false;
				}

				std::string fileContent;
				sff::read::tree::Table<StringIterator> fileTable;
				if (!loadSffFile(fileTable, *projectFile, fileContent, projectFilePath))
				{
					return false;
				}

				const auto projectVersion =
				    fileTable.getInteger<unsigned>("version", 1);
				if (projectVersion != 1)
				{
					ELOG("Unsupported project version: " << projectVersion);
					return false;
				}

				bool success = true;

				for (const auto &manager : managers)
				{
					mmo::String relativeFileName;

					auto *table = fileTable.getTable(manager.name);
					if (!table)
					{
						success = false;

						ELOG("File info of '" << manager.name << "' is missing in the project");
						continue;
					}

					if (!table->tryGetString("file", relativeFileName))
					{
						success = false;

						ELOG("File name of '" << manager.name << "' is missing in the project");
						continue;
					}
					
					const auto managerFile = directory.readFile(relativeFileName, false);
					if (!managerFile)
					{
						success = false;

						ELOG("Could not open file '" << relativeFileName << "'");
						continue;
					}
					if (!manager.load(*managerFile, relativeFileName, context))
					{
						ELOG("Could not load '" << manager.name << "'");
						success = false;
					}
				}

				return success &&
				       context.executeLoadLater();
			}

			template <class FileName>
			static bool loadSffFile(
			    sff::read::tree::Table<StringIterator> &fileTable,
			    std::istream &source,
			    std::string &content,
			    const FileName &fileName)
			{
				try
				{
					sff::loadTableFromFile(fileTable, content, source);
					return true;
				}
				catch (const sff::read::ParseException<StringIterator> &exception)
				{
					const auto line = std::count<StringIterator>(
					                      content.begin(),
					                      exception.position.begin,
					                      '\n');

					const mmo::String relevantLine(
					    exception.position.begin,
					    std::find<StringIterator>(exception.position.begin, content.end(), '\n'));

					ELOG("Error in SFF file " << fileName << ":" << (1 + line));
					ELOG("Parser error: " << exception.what() << " at '" << relevantLine << "'");
					return false;
				}
			}
		};
	}
}
