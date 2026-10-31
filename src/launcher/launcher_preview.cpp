// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "launcher_preview.h"
#include "launcher_view.h"
#include "launcher_layout.h"
#include "canvas.h"
#include "resource_data.h"
#include "resource.h"
#include "text_renderer.h"
#include "base/macros.h"
#include "log/default_log_levels.h"

#include <array>
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
			bool m_launched = false;
			bool SetDownloadPaused(const bool paused) override
			{
				m_paused = paused;
				return true;
			}
			bool m_paused = false;
			bool m_keepOpen = false;
		};

		/// Writes a little-endian value into a BMP header.
		template <typename T>
		void putLittleEndian(uint8* destination, const T value)
		{
			for (size_t i = 0; i < sizeof(T); ++i)
			{
				destination[i] = static_cast<uint8>(static_cast<uint64>(value) >> (i * 8));
			}
		}

		/// Writes a top-down 32bpp BI_RGB bitmap. The header is assembled by hand, not
		/// from the Win32 structs, so previews can be rendered on every platform.
		bool saveBitmap(const Bitmap& bitmap, const std::filesystem::path& path)
		{
			constexpr uint32 fileHeaderSize = 14;
			constexpr uint32 infoHeaderSize = 40;
			const uint32 pixelBytes = static_cast<uint32>(bitmap.GetWidth() * bitmap.GetHeight() * sizeof(Color));

			std::array<uint8, fileHeaderSize + infoHeaderSize> header{};
			header[0] = 'B';
			header[1] = 'M';
			putLittleEndian<uint32>(&header[2], fileHeaderSize + infoHeaderSize + pixelBytes);
			putLittleEndian<uint32>(&header[10], fileHeaderSize + infoHeaderSize);
			putLittleEndian<uint32>(&header[14], infoHeaderSize);
			putLittleEndian<int32>(&header[18], bitmap.GetWidth());
			// Negative height selects top-down row order.
			putLittleEndian<int32>(&header[22], -bitmap.GetHeight());
			putLittleEndian<uint16>(&header[26], 1);
			putLittleEndian<uint16>(&header[28], 32);

			std::ofstream file(path, std::ios::binary);
			file.write(reinterpret_cast<const char*>(header.data()), header.size());
			file.write(reinterpret_cast<const char*>(bitmap.GetPixels()), pixelBytes);
			return file.good();
		}
	} // namespace

	/// Verifies that every embedded asset can be found and decoded.
	///
	/// Runs on startup in debug builds so that a mismatch between resource.h,
	/// launcher.rc and the CMake asset list surfaces immediately rather than as a
	/// blank rectangle somewhere in the UI much later.
	void VerifyEmbeddedAssets()
	{
		constexpr std::array<uint32, 16> images = {
			IDR_PNG_SPLASH,		   IDR_PNG_PANEL_BOTTOM,	IDR_PNG_BORDER_FRAME,	IDR_PNG_BUTTON_UP,		  IDR_PNG_BUTTON_OVER,
			IDR_PNG_BUTTON_DOWN,   IDR_PNG_BUTTON_DISABLED, IDR_PNG_PROGRESS_TRACK, IDR_PNG_PROGRESS_FILL,	  IDR_PNG_ICON_CLOSE,
			IDR_PNG_ICON_MINIMIZE, IDR_PNG_CONTENT_TEXTURE, IDR_PNG_HERO_FRAME,		IDR_PNG_NEWS_DEVELOPMENT, IDR_PNG_NEWS_WORLD, IDR_PNG_BORDER_REFINED};

		for (const uint32 id : images)
		{
			const ResourceBlob blob = LoadResourceBlob(id);
			ASSERT(blob.IsValid() && "An embedded image resource is missing");

			Bitmap bitmap;
			VERIFY(Bitmap::DecodePng(blob, bitmap) && "An embedded image failed to decode");
			ASSERT(bitmap.IsValid());
		}

		for (const uint32 id : { IDR_TTF_DISPLAY, IDR_TTF_BODY })
		{
			const ResourceBlob blob = LoadResourceBlob(id);
			ASSERT(blob.IsValid() && "An embedded font resource is missing");

			FontFace face;
			VERIFY(face.Initialize(blob, 16) && "An embedded font failed to load");
			ASSERT(face.GetGlyph('A') != nullptr && "An embedded font produced no glyph");
		}

		DLOG("Embedded assets verified (" << images.size() << " images, 2 fonts)");
	}

	bool RenderLauncherPreviews(const std::string& directory)
	{
		std::error_code error;
		std::filesystem::create_directories(directory, error);
		if (error)
		{
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
			click(Point{110, 440});
			click(Point{780, 540});
			if (!host.m_keepOpen)
			{
				ELOG("Settings checkbox/save interaction failed");
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
