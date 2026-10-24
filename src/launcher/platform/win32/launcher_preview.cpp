// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "launcher_preview.h"
#include "launcher_view.h"
#include "launcher_layout.h"
#include "launcher_window.h"
#include "canvas.h"
#include "resource_data.h"
#include "resource.h"
#include "log/default_log_levels.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <cstring>

namespace mmo
{
	namespace
	{
		class PreviewHost final : public IPlatformHost
		{
		  public:
			void LaunchGame() override
			{
				m_launched = true;
			}
			void Minimize() override
			{
			}
			void Close() override
			{
			}
			void ShowMessage(const std::string&, const std::string&) override
			{
			}
			bool SaveKeepOpen(const bool keepOpen) override
			{
				m_keepOpen = keepOpen;
				return true;
			}
			bool GetKeepOpen() const override
			{
				return m_keepOpen;
			}
			ClientDisplaySettings GetDisplaySettings() const override
			{
				return m_displaySettings;
			}
			bool SaveDisplaySettings(const ClientDisplaySettings& settings) override
			{
				if (m_failDisplaySave)
				{
					return false;
				}
				m_displaySettings = settings;
				return settings.resolution == NormalizeClientResolution(settings.resolution).label;
			}
			ClientDisplaySettings m_displaySettings;
			bool m_failDisplaySave = false;
			bool m_launched = false;
			bool SetDownloadPaused(const bool paused) override
			{
				m_paused = paused;
				return true;
			}
			bool m_paused = false;
			bool m_keepOpen = false;
		};

		bool saveBitmap(const Bitmap& bitmap, const std::filesystem::path& path)
		{
			BITMAPFILEHEADER fileHeader{};
			fileHeader.bfType = 0x4D42;
			fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(BITMAPINFOHEADER);
			fileHeader.bfSize = fileHeader.bfOffBits + bitmap.GetWidth() * bitmap.GetHeight() * sizeof(Color);
			BITMAPINFOHEADER info{};
			info.biSize = sizeof(info);
			info.biWidth = bitmap.GetWidth();
			info.biHeight = -bitmap.GetHeight();
			info.biPlanes = 1;
			info.biBitCount = 32;
			std::ofstream file(path, std::ios::binary);
			file.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
			file.write(reinterpret_cast<const char*>(&info), sizeof(info));
			file.write(reinterpret_cast<const char*>(bitmap.GetPixels()), bitmap.GetWidth() * bitmap.GetHeight() * sizeof(Color));
			return file.good();
		}
	} // namespace

	bool RenderLauncherPreviews(const std::string& directory)
	{
		std::error_code error;
		std::filesystem::create_directories(directory, error);
		if (error)
		{
			return false;
		}
		// Exercise the real Win32 INI persistence without touching the installation.
		const auto previousDirectory = std::filesystem::current_path(error);
		if (error)
		{
			return false;
		}
		const auto settingsDirectory = std::filesystem::absolute(directory, error) / ("display-settings-" + std::to_string(GetCurrentProcessId()));
		if (error)
		{
			return false;
		}
		std::filesystem::create_directories(settingsDirectory, error);
		if (error)
		{
			return false;
		}
		std::filesystem::current_path(settingsDirectory, error);
		if (error)
		{
			return false;
		}
		const bool settingsValid = []
		{
			LauncherModel model;
			UpdateWorker worker(model, UpdateWorkerConfig{});
			LauncherWindow host(model, worker);
			if (host.GetDisplaySettings().resolution != "1280x720" || !host.GetDisplaySettings().windowed)
			{
				return false;
			}
			for (const auto& resolution : ClientResolutions)
			{
				for (const bool windowed : { false, true })
				{
					if (!host.SaveDisplaySettings({ resolution.label, windowed })
						|| host.GetDisplaySettings().resolution != resolution.label
						|| host.GetDisplaySettings().windowed != windowed)
					{
						return false;
					}
				}
			}
			return !host.SaveDisplaySettings({ "800x600", true });
		}();
		std::filesystem::current_path(previousDirectory, error);
		const bool restoredDirectory = !error;
		std::filesystem::remove(settingsDirectory / "launcher.ini", error);
		std::filesystem::remove(settingsDirectory, error);
		if (!settingsValid || !restoredDirectory || error)
		{
			ELOG("Launcher display settings persistence failed");
			return false;
		}
		for (const float dpi : {1.0f, 1.5f, 2.0f})
		{
			PreviewHost host;
			LauncherView view(host);
			if (!view.Initialize(dpi))
			{
				return false;
			}
			Bitmap surface(view.Scale(layout::WindowWidth), view.Scale(layout::WindowHeight));
			Canvas canvas(surface.GetPixels(), surface.GetWidth(), surface.GetHeight(), surface.GetWidth());
			UpdateSnapshot snapshot;
			snapshot.phase = UpdatePhase::Updating;
			snapshot.statusText = "Downloading files: 7754 / 10166";
			snapshot.downloadSizeText = "4.6 GB / 8.1 GB";
			snapshot.downloadRateText = "12.4 MB/s  |  ~4m 49s";
			snapshot.progress = 0.57f;
			view.ApplySnapshot(snapshot);
			view.Tick(10.0f);
			const auto capture = [&](const std::string& name)
			{
				view.Render(canvas);
				return saveBitmap(surface,
								  std::filesystem::path(directory) / (name + "-" + std::to_string(static_cast<int>(dpi * 100)) + ".bmp"));
			};
			const auto click = [&](const Point point)
			{
				view.OnMouseDown(point);
				view.OnMouseUp(point);
			};
			const char* names[] = {"home", "news", "patch-notes", "settings"};
			for (int i = 0; i < 4; ++i)
			{
				const Point tab{430 + i * 124, 74};
				if (view.HitTestCaption(tab))
				{
					ELOG("Navigation incorrectly starts a window drag");
					return false;
				}
				click(tab);
				if (!capture(names[i]))
				{
					return false;
				}
			}
			click(Point{597, 655});
			snapshot.paused = host.m_paused;
			view.ApplySnapshot(snapshot);
			if (!host.m_paused || !capture("paused"))
			{
				ELOG("Pause interaction failed");
				return false;
			}
			click(Point{597, 655});
			if (host.m_paused)
			{
				ELOG("Resume interaction failed");
				return false;
			}
			snapshot.paused = false;
			view.ApplySnapshot(snapshot);
			click(Point{400, 370});
			if (!capture("resolution-dropdown"))
			{
				return false;
			}
			for (int32 i = 0; i < static_cast<int32>(ClientResolutions.size()); ++i)
			{
				if (i != 0)
				{
					click(Point{400, 370});
				}
				click(Point{400, 406 + i * 32});
				click(Point{780, 540});
				if (host.m_displaySettings.resolution != ClientResolutions[i].label)
				{
					ELOG("Resolution selection/save interaction failed");
					return false;
				}
			}
			click(Point{664, 370});
			click(Point{780, 540});
			if (host.m_displaySettings.windowed)
			{
				return false;
			}
			// Unsaved edits must be discarded by Cancel.
			click(Point{664, 370});
			click(Point{400, 370});
			click(Point{400, 406});
			click(Point{980, 540});
			click(Point{780, 540});
			if (host.m_displaySettings.windowed || host.m_displaySettings.resolution != "1920x1080")
			{
				ELOG("Display settings Cancel failed");
				return false;
			}
			click(Point{400, 370});
			view.OnKey(40, false);
			view.OnKey(13, false);
			click(Point{780, 540});
			if (host.m_displaySettings.resolution != "1280x720")
			{
				ELOG("Resolution dropdown keyboard interaction failed");
				return false;
			}
			click(Point{400, 370});
			click(Point{400, 406});
			click(Point{664, 370});
			click(Point{110, 484});
			click(Point{780, 540});
			if (!host.m_keepOpen)
			{
				ELOG("Settings checkbox/save interaction failed");
				return false;
			}
			if (host.m_displaySettings.resolution != "1280x720" || !host.m_displaySettings.windowed || !capture("display-settings-saved"))
			{
				return false;
			}
			click(Point{554, 74});
			click(Point{300, 300});
			if (!capture("news-article"))
			{
				return false;
			}
			click(Point{678, 74});
			view.Render(canvas);
			Bitmap before = surface;
			view.OnScroll(300);
			view.Render(canvas);
			if (std::memcmp(before.GetPixels(), surface.GetPixels(), surface.GetWidth() * surface.GetHeight() * sizeof(Color)) == 0)
			{
				ELOG("Long patch notes did not scroll");
				return false;
			}
			if (!capture("patch-notes-scrolled"))
			{
				return false;
			}
			// Disabled Play must not launch a game.
			click(Point{950, 640});
			if (host.m_launched)
			{
				return false;
			}
			snapshot.phase = UpdatePhase::Ready;
			snapshot.statusText = "Game is up-to-date!";
			snapshot.progress = 1.0f;
			snapshot.playEnabled = true;
			view.ApplySnapshot(snapshot);
			view.Tick(10.0f);
			if (view.HasControlAt(Point{597, 655}))
			{
				ELOG("Pause control remains present after the update finishes");
				return false;
			}
			if (!capture("ready"))
			{
				return false;
			}
			host.m_failDisplaySave = true;
			click(Point{950, 640});
			if (host.m_launched)
			{
				ELOG("Play ignored a display-settings save failure");
				return false;
			}
			host.m_failDisplaySave = false;
			click(Point{950, 640});
			if (!host.m_launched)
			{
				return false;
			}
			view.OnKey(9, false);
			view.OnKey(13, false);
			if (!capture("keyboard-home"))
			{
				return false;
			}
			snapshot.phase = UpdatePhase::Failed;
			snapshot.statusText = "Could not reach the update server.";
			snapshot.noticeText = "Please check your internet connection and restart.";
			snapshot.progress = -1.0f;
			snapshot.playEnabled = false;
			view.ApplySnapshot(snapshot);
			if (!capture("failed"))
			{
				return false;
			}
			// Exercise live manifest application and artwork substitution without networking.
			auto manifest = std::make_shared<LauncherManifest>();
			if (!LauncherContentService::ParseManifest(
				R"({"version":1,"hero":{"image":"https://preview.example/world-v1.png","title":"LIVE CONTENT PREVIEW","subtitle":"The native layout stays the same"},"news":[{"title":"Published article","summary":"Read while downloading","body":"## News\nThis article came from a manifest.","image":"https://preview.example/world-v1.png"},{"title":"Fallback artwork","summary":"A missing image keeps embedded artwork","body":"An unavailable content image does not block the launcher.","image":"https://preview.example/missing.png"}],"patches":[{"title":"Published patch notes","summary":"Independent content refresh","body":"## Highlights\n- Remote content is visible.\n\n## Launcher improvements\n- The download footer stays unchanged."}]})",
				*manifest))
			{
				return false;
			}
			auto image = std::make_shared<Bitmap>();
			if (!Bitmap::DecodePng(LoadResourceBlob(IDR_PNG_NEWS_WORLD), *image))
			{
				return false;
			}
			auto content = std::make_shared<RemoteLauncherContent>();
			content->manifest = manifest;
			content->images.emplace(manifest->heroImage, image);
			view.ApplyRemoteContent(content);
			snapshot.phase = UpdatePhase::Updating;
			snapshot.statusText = "Downloading files: 7754 / 10166";
			snapshot.noticeText.clear();
			snapshot.progress = 0.57f;
			view.ApplySnapshot(snapshot);
			view.Tick(10.0f);
			click(Point{430, 74});
			if (!capture("remote-home"))
			{
				return false;
			}
			click(Point{554, 74});
			if (!capture("remote-news"))
			{
				return false;
			}
			click(Point{300, 300});
			if (!capture("remote-article"))
			{
				return false;
			}
			const Bitmap reader = surface;
			auto refreshed = std::make_shared<RemoteLauncherContent>(*content);
			refreshed->manifest = std::make_shared<LauncherManifest>(*manifest);
			refreshed->images.emplace("https://preview.example/missing.png", image);
			view.ApplyRemoteContent(refreshed);
			view.Render(canvas);
			if (std::memcmp(reader.GetPixels(), surface.GetPixels(), surface.GetWidth() * surface.GetHeight() * sizeof(Color)) != 0)
			{
				ELOG("An artwork or same-manifest refresh interrupted the article reader");
				return false;
			}
			click(Point{678, 74});
			if (!capture("remote-patch-notes"))
			{
				return false;
			}
		}
		return true;
	}
} // namespace mmo
