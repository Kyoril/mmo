// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "compile_directory.h"

#include "base/macros.h"
#include "base/sha1.h"

#include "simple_file_format/sff_load_file.h"
#include "simple_file_format/sff_read_tree.h"
#include "simple_file_format/sff_write_table.h"

#include "virtual_dir/reader.h"
#include "virtual_dir/writer.h"

#include "zstr/zstr.hpp"

#include <iostream>

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
						outStream = std::make_unique<std::ostream>(outputFile->rdbuf());
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
				// Obtain the source type so we can apply a different compiler eventually
				const auto type = inputDescription.getString("type");
				outputDescription.addKey("type", type);

				// Add sub directory entry
				const auto sub = inputDescription.getString("sub");

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
					// Read the from and to fields
					const auto from = inputDescription.getString("from");
					auto to = inputDescription.getString("to");
					if (to.empty())
					{
						// If there is no "to" location, use "from" as "to" location
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

							// If there is a subdirectory set, add it
							if (!sub.empty())
							{
								TableWriter entryOutput(entriesOutput, sff::write::Comma);
								entryOutput.addKey("type", "fs");
								entryOutput.addKey("name", sub);

								sff::write::Array<char> subEntriesOutput(
									entryOutput,
									"entries",
									sff::write::MultiLine);

								TableWriter entryDescriptionOutput(
									subEntriesOutput,
									sff::write::Comma);

								compileEntry(
									sourceRoot,
									outputRoot,
									*entryDescription,
									subFromLocation,
									entryDescriptionOutput,
									virtual_dir::joinPaths(subDestinationDir, sub),
									isZLibCompressed
								);

								subEntriesOutput.Finish();
								entryOutput.Finish();

								entryDescriptionOutput.Finish();
							}
							else
							{
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
						}

						entriesOutput.Finish();
					}
					else
					{
						// If there is a subdirectory set, add it
						auto dir = subDestinationDir;
						if (!sub.empty())
						{
							sff::write::Array<char> entriesOutput(
								outputDescription,
								"entries",
								sff::write::MultiLine);

							TableWriter entryOutput(entriesOutput, sff::write::Comma);
							entryOutput.addKey("type", "fs");
							entryOutput.addKey("name", sub);

							compileFile(
								sourceRoot,
								outputRoot,
								subFromLocation,
								entryOutput,
								virtual_dir::joinPaths(subDestinationDir, sub),
								isZLibCompressed,
								to
							);

							entryOutput.Finish();
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
		    bool isZLibCompressed,
		    const PatchVersionMetadata* versionMetadata
		)
		{
			// Try to find source.txt in source directoy and open it for reading
			const std::string fullSourceFileName = "source.txt";
			const auto sourceFile = sourceDir.readFile(fullSourceFileName, false);
			if (!sourceFile)
			{
				throw std::runtime_error("Could not open source list file " + fullSourceFileName);
			}

			// Parse the whole file
			std::string sourceContent;
			Table sourceTable;
			sff::loadTableFromFile(sourceTable, sourceContent, *sourceFile);

			// Check the format version
			const auto version = sourceTable.getInteger<unsigned>("version", 0);
			if (version == 0)
			{
				// Try to get the root object
				const auto *const root = sourceTable.getTable("root");
				if (!root)
				{
					throw std::runtime_error("Root directory entry is missing");
				}

				// Create the list.txt file in the target directory for writing. This file
				// will contain a summary of all file entries
				const virtual_dir::Path fullListFileName = "list.txt";
				const auto listFile = destinationDir.writeFile(fullListFileName, false, true);
				if (!listFile)
				{
					throw std::runtime_error(
					    "Could not open output list file " + fullListFileName);
				}

				// Write the target root table
				sff::write::Writer<char> listWriter(*listFile);
				TableWriter listTable(listWriter, sff::write::MultiLine);

				// Add the current file format verison
				listTable.addKey("version", 1);
				
				// Add version metadata if provided
				if (versionMetadata)
				{
					TableWriter metadataTable(listTable, "metadata", sff::write::Comma);
					metadataTable.addKey("patch_version", versionMetadata->version);
					metadataTable.addKey("build_date", versionMetadata->buildDate);
					metadataTable.addKey("git_commit", versionMetadata->gitCommit);
					
					if (versionMetadata->gitBranch)
					{
						metadataTable.addKey("git_branch", *versionMetadata->gitBranch);
					}
					
					if (versionMetadata->releaseNotes)
					{
						metadataTable.addKey("release_notes", *versionMetadata->releaseNotes);
					}
					
					metadataTable.Finish();
				}

				// Compile the first entry from the source list
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

				// And finishe the root entry in list.txt
				rootEntry.Finish();
			}
			else
			{
				throw std::runtime_error("Unsupported source list version");
			}
		}
	}
}
