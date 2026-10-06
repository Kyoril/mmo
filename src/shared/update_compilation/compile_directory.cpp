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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

namespace mmo
{
	namespace updating
	{
		namespace
		{
			typedef std::string::const_iterator Iterator;
			typedef sff::read::tree::Table<Iterator> Table;
			typedef sff::write::Table<char> TableWriter;

			/// A single file which has to be hashed and (optionally) compressed into the output directory.
			struct FileJob
			{
				virtual_dir::Path sourcePath;
				virtual_dir::Path outputPath;
			};

			/// The values a processed file contributes to list.txt.
			struct FileResult
			{
				std::uintmax_t originalSize = 0;
				std::string sha1Hex;
				std::uintmax_t compressedSize = 0;
			};

			/// The source tree is walked twice by the same code: the collect pass only gathers the
			/// files, which are then hashed and compressed in parallel, and the emit pass writes
			/// list.txt from those results in the original traversal order.
			struct CompileContext
			{
				virtual_dir::IReader &sourceRoot;
				virtual_dir::IWriter &outputRoot;
				bool isZLibCompressed;
				bool isCollecting;
				std::vector<FileJob> jobs;
				std::vector<FileResult> results;
				size_t nextResult;
			};

			/// Output directories which already exist, so that each one is created exactly once.
			struct CreatedDirectories
			{
				std::mutex mutex;
				std::set<virtual_dir::Path> paths;
			};

			FileResult processFile(
				virtual_dir::IReader &sourceRoot,
				virtual_dir::IWriter &outputRoot,
				CreatedDirectories &createdDirectories,
				const FileJob &job,
				bool isZLibCompressed
			)
			{
				const auto sourceFile = sourceRoot.readFile(job.sourcePath, false);
				if (!sourceFile)
				{
					throw std::runtime_error("Could not open source file " + job.sourcePath);
				}

				// Read the file once, it is needed for both hashing and compression
				std::string content;
				{
					sourceFile->seekg(0, std::ios::end);
					const std::streamoff size = sourceFile->tellg();
					sourceFile->seekg(0, std::ios::beg);

					content.resize(static_cast<size_t>(size));
					if (size > 0 && !sourceFile->read(content.data(), size))
					{
						throw std::runtime_error("Could not read source file " + job.sourcePath);
					}
				}

				FileResult result;
				result.originalSize = content.size();

				{
					const auto hashCode = sha1(content.data(), content.size());
					std::ostringstream formatter;
					sha1PrintHex(formatter, hashCode);
					result.sha1Hex = formatter.str();
				}

				// Creating directories is slow and not safe from several threads at once, so only the
				// first file of every directory creates it while the others wait for it.
				std::unique_ptr<std::ostream> outputFile;
				{
					const auto directory = virtual_dir::splitLeaf(job.outputPath).first;

					std::scoped_lock lock(createdDirectories.mutex);
					if (createdDirectories.paths.insert(directory).second)
					{
						outputFile = outputRoot.writeFile(job.outputPath, false, true);
					}
				}

				if (!outputFile)
				{
					outputFile = outputRoot.writeFile(job.outputPath, false, false);
				}

				if (!outputFile)
				{
					throw std::runtime_error("Could not open output file " + job.outputPath);
				}

				if (isZLibCompressed)
				{
					{
						zstr::ostream compressed(*outputFile);
						compressed.write(content.data(), static_cast<std::streamsize>(content.size()));
						compressed.flush();
					}

					result.compressedSize = static_cast<std::uintmax_t>(outputFile->tellp());
				}
				else
				{
					outputFile->write(content.data(), static_cast<std::streamsize>(content.size()));
				}

				if (!*outputFile)
				{
					throw std::runtime_error("Could not write output file " + job.outputPath);
				}

				return result;
			}

			void processFiles(CompileContext &context, unsigned threadCount)
			{
				const size_t jobCount = context.jobs.size();
				context.results.resize(jobCount);

				if (threadCount == 0)
				{
					threadCount = std::max(1u, std::thread::hardware_concurrency());
				}
				threadCount = static_cast<unsigned>(std::min<size_t>(threadCount, std::max<size_t>(jobCount, 1)));

				std::cout << "Processing " << jobCount << " files on " << threadCount << " threads..." << std::endl;
				const auto startTime = std::chrono::steady_clock::now();

				std::atomic<size_t> nextJob { 0 };
				std::atomic<size_t> finishedJobs { 0 };
				std::atomic<bool> failed { false };
				CreatedDirectories createdDirectories;
				std::mutex reportMutex;
				std::string firstError;

				auto worker = [&]()
				{
					while (!failed)
					{
						const size_t index = nextJob++;
						if (index >= jobCount)
						{
							break;
						}

						try
						{
							context.results[index] = processFile(
								context.sourceRoot,
								context.outputRoot,
								createdDirectories,
								context.jobs[index],
								context.isZLibCompressed);
						}
						catch (const std::exception &e)
						{
							std::scoped_lock lock(reportMutex);
							if (!failed.exchange(true))
							{
								firstError = e.what();
							}
							break;
						}

						const size_t finished = ++finishedJobs;
						if (finished % 1000 == 0)
						{
							std::scoped_lock lock(reportMutex);
							std::cout << "  " << finished << " / " << jobCount << std::endl;
						}
					}
				};

				std::vector<std::thread> threads;
				threads.reserve(threadCount - 1);
				for (unsigned i = 1; i < threadCount; ++i)
				{
					threads.emplace_back(worker);
				}

				// The calling thread is a worker as well
				worker();

				for (auto &thread : threads)
				{
					thread.join();
				}

				if (failed)
				{
					throw std::runtime_error(firstError);
				}

				const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - startTime);
				std::cout << "Processed " << jobCount << " files in " << elapsed.count() << " ms" << std::endl;
			}

			void compileFile(
				CompileContext &context,
				const virtual_dir::Path &fromLocation,
				TableWriter &outputDescription,
				const virtual_dir::Path &destinationDir,
				const std::string &fileName
			)
			{
				const auto type = context.sourceRoot.getType(fromLocation);

				if (type == virtual_dir::file_type::Directory)
				{
					sff::write::Array<char> entriesOutput(
					    outputDescription,
					    "entries",
					    sff::write::MultiLine);

					const auto entries = context.sourceRoot.queryEntries(
					                         fromLocation
					                     );

					for (const auto & entry : entries)
					{
						TableWriter entryOutput(entriesOutput, sff::write::Comma);
						entryOutput.addKey("type", "fs");
						entryOutput.addKey("name", entry);

						compileFile(
						    context,
						    virtual_dir::joinPaths(fromLocation, entry),
						    entryOutput,
						    virtual_dir::joinPaths(destinationDir, entry),
						    entry
						);

						entryOutput.Finish();
					}

					entriesOutput.Finish();
				}
				else if (type == virtual_dir::file_type::File)
				{
					auto compressedNameFull = destinationDir;
					if (context.isZLibCompressed)
					{
						const auto compressedName = fileName + ".z";
						outputDescription.addKey("compressedName", compressedName);

						compressedNameFull += ".z";
					}

					if (context.isCollecting)
					{
						context.jobs.push_back(FileJob { fromLocation, compressedNameFull });
						return;
					}

					if (context.nextResult >= context.results.size())
					{
						throw std::runtime_error("Source tree changed while compiling the update");
					}

					const auto &result = context.results[context.nextResult++];

					outputDescription.addKey("originalSize", result.originalSize);
					outputDescription.addKey("sha1", result.sha1Hex);

					if (context.isZLibCompressed)
					{
						outputDescription.addKey("compression", "zlib");
						outputDescription.addKey("compressedSize", result.compressedSize);
					}
				}
			}

			void compileIf(
			    CompileContext &context,
			    const Table &inputDescription,
			    const virtual_dir::Path &fromLocation,
			    TableWriter &outputDescription,
			    const virtual_dir::Path &destinationDir
			);


			void compileEntry(
			    CompileContext &context,
			    const Table &inputDescription,
			    const virtual_dir::Path &fromLocation,
			    TableWriter &outputDescription,
			    const virtual_dir::Path &destinationDir
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
					    context,
					    inputDescription,
					    fromLocation,
					    outputDescription,
					    destinationDir
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
									context,
									*entryDescription,
									subFromLocation,
									entryDescriptionOutput,
									virtual_dir::joinPaths(subDestinationDir, sub)
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
									context,
									*entryDescription,
									subFromLocation,
									entryDescriptionOutput,
									subDestinationDir
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
								context,
								subFromLocation,
								entryOutput,
								virtual_dir::joinPaths(subDestinationDir, sub),
								to
							);

							entryOutput.Finish();
							entriesOutput.Finish();
						}
						else
						{
							compileFile(
								context,
								subFromLocation,
								outputDescription,
								subDestinationDir,
								to
							);
						}
					}
				}
			}


			void compileIf(
			    CompileContext &context,
			    const Table &inputDescription,
			    const virtual_dir::Path &fromLocation,
			    TableWriter &outputDescription,
			    const virtual_dir::Path &destinationDir
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
				    context,
				    *value,
				    fromLocation,
				    valueOutput,
				    destinationDir
				);

				valueOutput.Finish();
			}

			/// Writes the complete list description for the root entry. Used by both passes.
			void writeList(CompileContext &context, const Table &root, std::ostream &listFile)
			{
				sff::write::Writer<char> listWriter(listFile);
				TableWriter listTable(listWriter, sff::write::MultiLine);

				// Add the current file format verison
				listTable.addKey("version", 1);

				// Compile the first entry from the source list
				TableWriter rootEntry(listTable, "root", sff::write::Comma);
				compileEntry(
				    context,
				    root,
				    "",
				    rootEntry,
				    ""
				);

				// And finishe the root entry in list.txt
				rootEntry.Finish();
			}
		}


		void compileDirectory(
			virtual_dir::IReader &sourceDir,
			virtual_dir::IWriter &destinationDir,
		    bool isZLibCompressed,
			unsigned threadCount
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

				CompileContext context { sourceDir, destinationDir, isZLibCompressed, true, {}, {}, 0 };

				// Collect pass: walk the tree to find every file, the list it produces is discarded
				{
					std::ostringstream discardedList;
					writeList(context, *root, discardedList);
				}

				// Hash and compress all files in parallel
				processFiles(context, threadCount);

				// Create the list.txt file in the target directory for writing. This file
				// will contain a summary of all file entries
				const virtual_dir::Path fullListFileName = "list.txt";
				const auto listFile = destinationDir.writeFile(fullListFileName, false, true);
				if (!listFile)
				{
					throw std::runtime_error(
					    "Could not open output list file " + fullListFileName);
				}

				// Emit pass: same traversal, now filled in with the results
				context.isCollecting = false;
				writeList(context, *root, *listFile);

				if (context.nextResult != context.results.size())
				{
					throw std::runtime_error("Source tree changed while compiling the update");
				}
			}
			else
			{
				throw std::runtime_error("Unsupported source list version");
			}
		}
	}
}
