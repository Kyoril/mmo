// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#ifdef __APPLE__

// This is for apple support since we need to use objective-c which requires
// the file to use the extension "*.m" oder "*.mm"
extern int main_osx(int argc, char* argv[]);

int main(int argc, char *argv[])
{
	// Mac OS X
	return main_osx(argc, argv);
}

#else

// Include asio before windows header files
#include "asio/io_service.hpp"

// Include windows header files
#include <Windows.h>
#include <commctrl.h>
#include "winnls.h"
#include "objbase.h"
#include "objidl.h"
#include "shlguid.h"
#include "resource.h"
#include <ShlObj.h>
#include "shobjidl.h"

#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>
#include <chrono>
#include <memory>
#include <optional>
#include <any>
#include <array>
#include <set>
#include "asio.hpp"

#include "cxxopts/cxxopts.hpp"
#include "base/filesystem.h"
#include "base/signal.h"

#include "base/typedefs.h"
#include "base/macros.h"

#include "log/log.h"
#include "log/default_log.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"
#include "log/log_entry.h"

#include "updater/open_source_from_url.h"
#include "updater/update_url.h"
#include "updater/prepare_parameters.h"
#include "updater/update_parameters.h"
#include "updater/prepare_update.h"
#include "updater/update_source.h"
#include "updater/updater_progress_handler.h"
#include "updater/prepare_progress_handler.h"
#include "updater/update_application.h"

#include "base/win_utility.h"

#define MMO_LAUNCHER_VERSION 1

namespace
{
	const std::string UpdateSourceUrl =
	    "http://patch.mmo-dev.net/update"
	    ;
	
	bool isSelfUpdateEnabled = true;
	std::thread updatingThread;
	size_t updatePerformanceConcurrency = 1;
	std::string previousExecutableToBeRemoved;
	bool doRetryRemovePreviousExecutable = false;
	HWND dialogHandle = NULL;
	std::uintmax_t updateSize = 0;
	std::uintmax_t updated = 0;
	std::uintmax_t lastUpdateStatus = 0;

	void ShowVersionInfoDialog()
	{
		const std::string caption = "MMORPG Launcher";
		const std::string body =
		    "Version: 1\n"
		    "Build date: " __DATE__ " " __TIME__ "\n"
#ifndef NDEBUG
		    "Debug configuration\n"
#endif
		    ;

		MessageBoxA(
		    nullptr,
		    body.c_str(),
		    caption.c_str(),
		    MB_OK
		);
	}
	
	void RemovePreviousExecutable()
	{
		try
		{
			std::this_thread::sleep_for(std::chrono::seconds(1));
			std::filesystem::remove(previousExecutableToBeRemoved);
		}
		catch (const std::exception &e)
		{
			std::cerr << e.what() << '\n';
			//this error is not very important, so we do not exit here

			doRetryRemovePreviousExecutable = true;
		}
	}
}

INT_PTR CALLBACK MainDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

namespace mmo
{
	namespace updating
	{
		struct Win32ProgressHandler
			: IPrepareProgressHandler
			, IUpdaterProgressHandler
		{
			virtual void updateFile(const std::string &name, std::uintmax_t size, std::uintmax_t loaded) override
			{
				std::scoped_lock guiLock{ m_guiMutex };

				// Increment download counter
				if (loaded > lastUpdateStatus)
				{
					updated += loaded - lastUpdateStatus;
				}

				// Update last update status
				lastUpdateStatus = loaded;

				// Status string
				std::stringstream statusStream;
				statusStream << "Updating file " << name << "...";
				SetDlgItemTextA(dialogHandle, IDC_STATUS_LABEL, statusStream.str().c_str());

				const auto loadedMB = static_cast<float>(loaded) / 1024.0f / 1024.0f;
				const auto sizeMB = static_cast<float>(size) / 1024.0f / 1024.0f;

				// Current file
				std::stringstream currentStream;
				currentStream << std::fixed << std::setprecision(2) << loadedMB << " / " << std::setprecision(2) << sizeMB << " MB";
				SetDlgItemTextA(dialogHandle, IDC_CURRENT, currentStream.str().c_str());

				// Progress bar
				int percent = static_cast<int>(static_cast<float>(updated) / static_cast<float>(updateSize) * 100.0f);
				SendMessageA(GetDlgItem(dialogHandle, IDC_PROGRESS_BAR), PBM_SETPOS, percent, 0);

				const auto updatedMb = static_cast<float>(updated) / 1024.0f / 1024.0f;
				const auto updateSizeMb = static_cast<float>(updateSize) / 1024.0f / 1024.0f;
				
				// Overall progress
				std::stringstream totalStream;
				totalStream << std::fixed << std::setprecision(2) << updatedMb << " / " << std::setprecision(2) << updateSizeMb << " MB (" << percent << "%)";
				SetDlgItemTextA(dialogHandle, IDC_TOTAL, totalStream.str().c_str());

				// Log file process
				if (loaded >= size)
				{
					// Reset counter
					lastUpdateStatus = 0;
					ILOG("Successfully loaded file " << name << " (Size: " << size << " bytes)");
				}
			}

			virtual void beginCheckLocalCopy(const std::string &name) override
			{
			}

		private:

			std::mutex m_guiMutex;
		};
	}

	static void createDirectory(const std::filesystem::path &directory)
	{
		try
		{
			std::filesystem::create_directories(directory);
		}
		catch (const std::filesystem::filesystem_error &e)
		{
			ELOG(e.what());
		}
	}
}

#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	cxxopts::Options options("Available options");
	options.add_options()
		("v,version", "Displays the version of the launcher on screen.")
		("remove-previous", "Tries to remove a specified file", cxxopts::value<std::string>(previousExecutableToBeRemoved))
		("no-self-update", "Disables self-update of the launcher executable")
		("j,concurrency", "The number of threads used for downloading and updating", cxxopts::value<size_t>(updatePerformanceConcurrency))
		;

	try
	{
		int argc = 0;
		PCHAR* argv = CommandLineToArgvA(GetCommandLineA(), &argc);

		auto results = options.parse(argc, argv);

		if (results.count("version"))
		{
			ShowVersionInfoDialog();
		}

		if (results.count("no-self-update"))
		{
			isSelfUpdateEnabled = false;
		}

		if (!previousExecutableToBeRemoved.empty())
		{
			RemovePreviousExecutable();
		}

		// Documents root
		std::filesystem::path documentsPath(".");
		std::array<char, MAX_PATH> documents = { "." };
#ifdef _MSC_VER
		SHGetFolderPathA(0, CSIDL_MYDOCUMENTS, 0, SHGFP_TYPE_CURRENT, documents.data());
#endif
		// Assign
		documentsPath = documents.data();
		documentsPath /= "MMORPG";

		const auto logDir = (documentsPath / "Logs");
		mmo::createDirectory(logDir);

		//generic log
		std::ofstream logFile;
		{
			const auto filename = logDir / "Launcher.log";
			logFile.open(filename.string());
			if (logFile)
			{
				auto options = mmo::g_DefaultFileLogOptions;
				options.alwaysFlush = true;

				mmo::g_DefaultLog.signal().connect(std::bind(mmo::printLogEntry,
					std::ref(logFile),
					std::placeholders::_1, //the message
					options));
			}
		}

		// Show the dialog
		DialogBoxA(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, MainDlgProc);

		// Wait for the update thread to terminate
		updatingThread.join();
	}
	catch (const cxxopts::OptionException& ex)
	{
		ELOG("Command line option exception: " << ex.what());
	}

	return 0;
}

// Code from MSDN
HRESULT CreateLink(LPCSTR lpszPathObj, LPCSTR lpszPathLink, LPCSTR lpszDesc, LPCSTR lpszWorkingDir)
{
	if (FAILED(CoInitialize(NULL)))
	{
		MessageBoxA(NULL, "Failed to initialize!", "Error", MB_ICONERROR | MB_OK);
		return 1;
	}

	// Get a pointer to the IShellLink interface. It is assumed that CoInitialize
	// has already been called.
	IShellLink *psl;
	HRESULT hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID *)&psl);
	if (SUCCEEDED(hres))
	{
		// Set the path to the shortcut target and add the description.
		psl->SetPath(lpszPathObj);
		psl->SetDescription(lpszDesc);
		psl->SetWorkingDirectory(lpszWorkingDir);

		// Query IShellLink for the IPersistFile interface, used for saving the
		// shortcut in persistent storage.
		IPersistFile *ppf = nullptr;
		hres = psl->QueryInterface(IID_IPersistFile, (LPVOID *)&ppf);

		if (SUCCEEDED(hres))
		{
			std::array<WCHAR, MAX_PATH> wsz;

			// Ensure that the string is Unicode.
			MultiByteToWideChar(CP_ACP, 0, lpszPathLink, -1, wsz.data(), wsz.size());

			// Add code here to check return value from MultiByteWideChar
			// for success.

			// Save the link by calling IPersistFile::Save.
			hres = ppf->Save(wsz.data(), TRUE);
			ppf->Release();
		}
		psl->Release();
	}
	return hres;
}

namespace mmo
{
	void createDesktopShortcut()
	{
#if _MSC_VER
		std::array<CHAR, MAX_PATH> buffer = {{}};
		SHGetFolderPathA(0, CSIDL_DESKTOPDIRECTORY, 0, SHGFP_TYPE_CURRENT, buffer.data());
		std::filesystem::path desktopPath = buffer.data();
		desktopPath /= "MMORPG.lnk";

		const std::filesystem::path workingDir = std::filesystem::current_path();

		// Create desktop link
		if (FAILED(CreateLink((workingDir / "Launcher.exe").string().c_str(),
				                desktopPath.string().c_str(),
				                "Play MMORPG",
				                workingDir.string().c_str())))
		{
			// TODO: Could not create desktop link
		}
#endif
	}

	//Warum gibt die Funktion etwas zurück?
	bool performUpdateThread()
	{
		ILOG("Connecting to the update server...");

		const std::string outputDir = "./";
		std::set<std::string> conditionsSet;
		conditionsSet.insert("WINDOWS");

		// Check windows
		if (sizeof(void *) == 8)
		{
			conditionsSet.insert("X64");
		}
		else
		{
			conditionsSet.insert("X86");
		}

		bool doUnpackArchives = false;

		mmo::updating::Win32ProgressHandler progressHandler;

		auto source = mmo::updating::openSourceFromUrl(
			mmo::updating::UpdateURL(UpdateSourceUrl)
		);

		ILOG("Preparing data...");

		mmo::updating::PrepareParameters prepareParameters(
		    std::move(source),
		    conditionsSet,
		    doUnpackArchives,
		    progressHandler
		);

		ILOG("Updating files...");

		try
		{
			const auto preparedUpdate = mmo::updating::prepareUpdate(
				outputDir,
				prepareParameters
			);

			// Get download size
			updateSize = preparedUpdate.estimates.updateSize;
			DLOG("Download size: " << preparedUpdate.estimates.downloadSize);
			DLOG("Update size: " << updateSize);

			mmo::updating::UpdateParameters updateParameters(
			    std::move(prepareParameters.source),
			    doUnpackArchives,
			    progressHandler
			);

			const auto selfExecutablePath = []() -> std::string
			{
				std::array<char, MAX_PATH> buffer = {{}};
				GetModuleFileNameA(
				    nullptr,
				    buffer.data(),
				    buffer.size());
				return buffer.data();
			}();

			ASSERT(!selfExecutablePath.empty());

			if (isSelfUpdateEnabled)
			{
				const auto selfUpdate = updateApplication(
					selfExecutablePath,
					preparedUpdate
				);

				if (selfUpdate.perform)
				{
					MessageBoxA(
					    dialogHandle,
					    "A new launcher version is available! The launcher will be restarted...",
					    "New launcher version available",
					    MB_OK | MB_ICONINFORMATION);

					//TODO: argc und argv?
					selfUpdate.perform(
					    updateParameters,
					    nullptr,
					    0
					);

					EndDialog(dialogHandle, 0);
					return true;
				}
			}

			{
				asio::io_service dispatcher;
				for (const auto & step : preparedUpdate.steps)
				{
					dispatcher.post(
					    [&]()
					{
						std::string errorMessage;
						try
						{
							if (!isSelfUpdateEnabled)
							{
								try
								{
									if (std::filesystem::equivalent(
									            step.destinationPath,
									            selfExecutablePath
									        ))
									{
										return;
									}
								}
								catch (const std::filesystem::filesystem_error &)
								{
									//ignore
								}
							}

							while (step.step(updateParameters)) {
								;
							}

							return;
						}
						catch (const std::exception &ex)
						{
							errorMessage = ex.what();
						}
						catch (...)
						{
							errorMessage = "Caught an exception not derived from std::exception";
						}

						dispatcher.stop();
						dispatcher.post([errorMessage]()
						{
							throw std::runtime_error(errorMessage);
						});
					});
				}

				std::vector<std::thread> threads;
				std::generate_n(
				    std::back_inserter(threads),
				    updatePerformanceConcurrency,
					[&dispatcher]() {
						return std::thread([&dispatcher] { dispatcher.run(); });
					}
				);

				std::for_each(
				    threads.begin(),
				    threads.end(),
				    std::bind(&std::thread::join, std::placeholders::_1));

				dispatcher.run();
			}

			DLOG("Updated " << updated << " / " << updateSize << " bytes");

			// Set progress
			std::stringstream statusStream;
			statusStream << "Game is up-to-date!";
			SetDlgItemTextA(dialogHandle, IDC_STATUS_LABEL, statusStream.str().c_str());
			SendMessageA(GetDlgItem(dialogHandle, IDC_PROGRESS_BAR), PBM_SETPOS, 100, 0);

			// Log
			ILOG(statusStream.str());

			// Update buttons
			EnableWindow(GetDlgItem(dialogHandle, IDC_PLAY), TRUE);

			if (doRetryRemovePreviousExecutable)
			{
				RemovePreviousExecutable();
			}
		}
		catch (const std::exception &e)
		{
			// Set progress
			std::stringstream statusStream;
			statusStream << e.what();
			SetDlgItemTextA(dialogHandle, IDC_STATUS_LABEL, statusStream.str().c_str());

			// Log error
			ELOG(e.what());
			return false;
		}

		return true;
	}
}

INT_PTR CALLBACK MainDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_INITDIALOG:
		{
			dialogHandle = hDlg;
			SendMessage(GetDlgItem(dialogHandle, IDC_PROGRESS_BAR), PBM_SETRANGE, 0, MAKELPARAM(0, 100));

			updatingThread = std::thread(mmo::performUpdateThread);
			return TRUE;
		}

	case WM_CLOSE:
		EndDialog(hDlg, 0);
		return TRUE;

	case WM_COMMAND:
		{
			switch (LOWORD(wParam))
			{
			case IDC_CLOSE:
				{
					EndDialog(hDlg, 0);
					return TRUE;
				}

			case IDC_PLAY:
				{
					// Start the client (TODO?)
					WinExec("./mmo_client.exe -uptodate", SW_SHOWDEFAULT);
					EndDialog(hDlg, 0);
					return FALSE;
				}

			case IDC_CREATE_SHORTCUT:
				{
					mmo::createDesktopShortcut();
					return FALSE;
				}
			}
			break;
		}

	case WM_DESTROY:
		{
			PostQuitMessage(0);
			return TRUE;
		}
	}

	return FALSE;
}

#endif	//__APPLE__