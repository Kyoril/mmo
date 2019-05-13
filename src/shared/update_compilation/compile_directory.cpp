
#include "compile_directory.h"

#include "base/macros.h"
#include "base/sha1.h"

#include "simple_file_format/sff_load_file.h"
#include "simple_file_format/sff_read_tree.h"
#include "simple_file_format/sff_write_table.h"

#include "virtual_dir/reader.h"
#include "virtual_dir/writer.h"

#include "zstr/zstr.hpp"

namespace mmo
{
	namespace updating
	{
		namespace
		{
			typedef std::string::const_iterator Iterator;
			typedef sff::read::tree::Table<Iterator> Table;
			typedef sff::write::Table<char> TableWriter;


			void compileFile(
				virtual_dir::IReader &sourceRoot,
				virtual_dir::IWriter &outputRoot,
				const virtual_dir::Path &fromLocation,
				TableWriter &outputDescription,
				const virtual_dir::Path &destinationDir,
				bool isZLibCompressed,
				const std::string &fileName
			)
			{
				const auto type = sourceRoot.getType(fromLocation);

				if (type == virtual_dir::file_type::Directory)
				{
					sff::write::Array<char> entriesOutput(
					    outputDescription,
					    "entries",
					    sff::write::MultiLine);

					const auto entries = sourceRoot.queryEntries(
					                         fromLocation
					                     );

					for (const auto & entry : entries)
					{
						TableWriter entryOutput(entriesOutput, sff::write::Comma);
						entryOutput.addKey("type", "fs");
						entryOutput.addKey("name", entry);

						compileFile(
						    sourceRoot,
						    outputRoot,
						    virtual_dir::joinPaths(fromLocation, entry),
						    entryOutput,
						    virtual_dir::joinPaths(destinationDir, entry),
						    isZLibCompressed,
						    entry
						);

						entryOutput.Finish();
					}

					entriesOutput.Finish();
				}
				else if (type == virtual_dir::file_type::File)
				{
					const auto sourceFile = sourceRoot.readFile(
					                            fromLocation,
					                            false
					                        );

					if (!sourceFile)
					{
						throw std::runtime_error(
						    "Could not open source file " +
						    fromLocation);
					}

					auto compressedNameFull = destinationDir;
					if (isZLibCompressed)
					{
						const auto compressedName = fileName + ".z";
						outputDescription.addKey("compressedName", compressedName);

						compressedNameFull += ".z";
					}

					{
						sourceFile->seekg(0, std::ios::end);
						{
							const std::streamoff originalSize = sourceFile->tellg();
							outputDescription.addKey("originalSize", originalSize);
						}

						sourceFile->seekg(0, std::ios::beg);
						{
							const auto hashCode = sha1(*sourceFile);
							std::ostringstream formatter;
							sha1PrintHex(formatter, hashCode);
							outputDescription.addKey("sha1", formatter.str());
						}
					}

					//workaround:
					const auto outputFile = outputRoot.writeFile(
					                            compressedNameFull,
					                            false,
					                            true
					                        );

					if (!outputFile)
					{
						throw std::runtime_error(
						    "Could not open output file " +
						    compressedNameFull);
					}

					sourceFile->clear();
					sourceFile->seekg(0, std::ios::beg);


					// Generate the output stream with compression if requested
					std::unique_ptr<std::ostream> outStream;
					if (!isZLibCompressed)
					{
						outStream = std::make_unique<std::ostream>(outputFile->rdbuf(), true);
					}
					else
					{
						outStream = std::make_unique<zstr::ostream>(*outputFile);
					}

					// Read in source file in 4k byte chunks and process it
					char buf[4096];
					std::streamsize read;
					do
					{
						sourceFile->read(buf, 4096);
						read = sourceFile->gcount();
						if (read > 0)
						{
							outStream->write(buf, read);
						}
					} while (*sourceFile && read > 0);

					// Flush the output stream
					outStream->flush();
					outStream.reset();

					if (isZLibCompressed)
					{
						outputDescription.addKey("compression", "zlib");

						const std::uintmax_t compressedSize = outputFile->tellp();
						outputDescription.addKey("compressedSize", compressedSize);
					}
				}
			}

			void compileIf(
			    virtual_dir::IReader &sourceRoot,
			    virtual_dir::IWriter &outputRoot,
			    const Table &inputDescription,
			    const virtual_dir::Path &fromLocation,
			    TableWriter &outputDescription,
			    const virtual_dir::Path &destinationDir,
			    bool isZLibCompressed
			);

			void compileEntry(
			    virtual_dir::IReader &sourceRoot,
			    virtual_dir::IWriter &outputRoot,
			    const Table &inputDescription,
			    const virtual_dir::Path &fromLocation,
			    TableWriter &outputDescription,
			    const virtual_dir::Path &destinationDir,
			    bool isZLibCompressed
			)
			{
				const auto type = inputDescription.getString("type");
				outputDescription.addKey("type", type);

				if (type == "if")
				{
					compileIf(
					    sourceRoot,
					    outputRoot,
					    inputDescription,
					    fromLocation,
					    outputDescription,
					    destinationDir,
					    isZLibCompressed
					);
				}
				else
				{
					const auto from = inputDescription.getString("from");
					auto to = inputDescription.getString("to");
					if (to.empty())
					{
						to = from;
					}

					outputDescription.addKey("name", to);

					const auto subFromLocation = virtual_dir::joinPaths(fromLocation, from);
					const auto subDestinationDir = virtual_dir::joinPaths(destinationDir, to);

					if (const auto *const entries = inputDescription.getArray("entries"))
					{
						sff::write::Array<char> entriesOutput(
						    outputDescription,
						    "entries",
						    sff::write::MultiLine);

						for (size_t i = 0, c = entries->getSize(); i < c; ++i)
						{
							const auto *const entryDescription = entries->getTable(i);
							if (!entryDescription)
							{
								throw std::runtime_error("Found a non-table in an 'entries' array");
							}

							TableWriter entryDescriptionOutput(
							    entriesOutput,
							    sff::write::Comma);

							compileEntry(
							    sourceRoot,
							    outputRoot,
							    *entryDescription,
							    subFromLocation,
							    entryDescriptionOutput,
							    subDestinationDir,
							    isZLibCompressed
							);

							entryDescriptionOutput.Finish();
						}

						entriesOutput.Finish();
					}
					else
					{
						compileFile(
						    sourceRoot,
						    outputRoot,
						    subFromLocation,
						    outputDescription,
						    subDestinationDir,
						    isZLibCompressed,
						    to
						);
					}
				}
			}

			void compileIf(
			    virtual_dir::IReader &sourceRoot,
			    virtual_dir::IWriter &outputRoot,
			    const Table &inputDescription,
			    const virtual_dir::Path &fromLocation,
			    TableWriter &outputDescription,
			    const virtual_dir::Path &destinationDir,
			    bool isZLibCompressed
			)
			{
				{
					std::string condition;
					if (!inputDescription.tryGetString("condition", condition))
					{
						throw std::runtime_error("'if' condition missing");
					}
					outputDescription.addKey("condition", condition);
				}

				const auto *const value = inputDescription.getTable("value");
				if (!value)
				{
					throw std::runtime_error("'if' value missing");
				}

				TableWriter valueOutput(
				    outputDescription,
				    "value",
				    sff::write::Comma);

				compileEntry(
				    sourceRoot,
				    outputRoot,
				    *value,
				    fromLocation,
				    valueOutput,
				    destinationDir,
				    isZLibCompressed
				);

				valueOutput.Finish();
			}
		}

		void compileDirectory(
			virtual_dir::IReader &sourceDir,
			virtual_dir::IWriter &destinationDir,
		    bool isZLibCompressed
		)
		{
			const std::string fullSourceFileName = "source.txt";
			const auto sourceFile = sourceDir.readFile(fullSourceFileName, false);
			if (!sourceFile)
			{
				throw std::runtime_error("Could not open source list file " + fullSourceFileName);
			}

			std::string sourceContent;
			Table sourceTable;
			sff::loadTableFromFile(sourceTable, sourceContent, *sourceFile);

			const auto version = sourceTable.getInteger<unsigned>("version", 0);
			if (version == 0)
			{
				const auto *const root = sourceTable.getTable("root");
				if (!root)
				{
					throw std::runtime_error("Root directory entry is missing");
				}

				const virtual_dir::Path fullListFileName = "list.txt";
				const auto listFile = destinationDir.writeFile(fullListFileName, false, true);

				if (!listFile)
				{
					throw std::runtime_error(
					    "Could not open output list file " + fullListFileName);
				}

				sff::write::Writer<char> listWriter(*listFile);
				TableWriter listTable(listWriter, sff::write::MultiLine);

				listTable.addKey("version", 1);

				TableWriter rootEntry(listTable, "root", sff::write::Comma);
				compileEntry(
				    sourceDir,
				    destinationDir,
				    *root,
				    "",
				    rootEntry,
				    "",
				    isZLibCompressed
				);
				rootEntry.Finish();
			}
			else
			{
				throw std::runtime_error("Unsupported source list version");
			}
		}
	}
}
