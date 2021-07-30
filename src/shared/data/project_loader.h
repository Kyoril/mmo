
#pragma once

#include "base/typedefs.h"
#include "virtual_dir/file_system_reader.h"
#include "simple_file_format/sff_read_tree.h"
#include "simple_file_format/sff_load_file.h"
#include "log/default_log_levels.h"
#include "google/protobuf/io/coded_stream.h"

namespace mmo
{
	/// Context for several functionalities during the load process.
	struct DataLoadContext
	{
	private:

		DataLoadContext(const DataLoadContext &Other) = delete;
		DataLoadContext &operator=(const DataLoadContext &Other) = delete;

	public:

		typedef std::function<void(const String &)> OnError;
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

		bool ExecuteLoadLater()
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
			typedef std::function < bool(std::istream &file, const String &fileName, const String &hash, Context &context) > LoadFunction;

			String name;
			LoadFunction load;

			template<typename T>
			ManagerEntry(
				const String &name,
				T &manager
			)
				: name(name)
				, load([this, name, &manager](
					        std::istream & file,
					        const String & fileName,
							const String & hash,
					        Context & context) mutable -> bool
			{
				return this->LoadManagerFromFile(file, fileName, hash, context, manager, name);
			})
			{
			}

		private:

			template<typename T>
			static bool LoadManagerFromFile(
				std::istream &file,
				const String &fileName,
				const String &hash,
				Context &context,
				T &manager,
				const String &arrayName)
			{
				manager.hashString = hash;
				return manager.Load(file);
			}
		};

		typedef std::vector<ManagerEntry> Managers;

		typedef String::const_iterator StringIterator;

		static bool Load(virtual_dir::IReader &directory, const Managers &managers, Context &context)
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
			if (!LoadSffFile(fileTable, *projectFile, fileContent, projectFilePath))
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
				String relativeFileName;
				String hashString;

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

				table->tryGetString("sha1", hashString);

				const auto managerFile = directory.readFile(relativeFileName, false);
				if (!managerFile)
				{
					success = false;

					ELOG("Could not open file '" << relativeFileName << "'");
					continue;
				}
				if (!manager.load(*managerFile, relativeFileName, hashString, context))
				{
					ELOG("Could not load '" << manager.name << "'");
					success = false;
				}
			}

			return success && context.ExecuteLoadLater();
		}

		template <class FileName>
		static bool LoadSffFile(
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

				const String relevantLine(
					exception.position.begin,
					std::find<StringIterator>(exception.position.begin, content.end(), '\n'));

				ELOG("Error in SFF file " << fileName << ":" << (1 + line));
				ELOG("Parser error: " << exception.what() << " at '" << relevantLine << "'");
				return false;
			}
		}
	};
}
