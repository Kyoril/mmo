// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "page_bake_utils.h"

#include "assets/asset_registry.h"
#include "graphics/graphics_device.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/movable_object.h"
#include "scene_graph/scene_node.h"
#include "stb_dxt.h"
#include "stream_sink.h"
#include "terrain/page.h"
#include "tex_v1_0/header_save.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace mmo
{
	namespace page_bake
	{
		float HalfToFloat(const uint16 half)
		{
			const uint32 sign = (half & 0x8000u) << 16;
			const uint32 exponent = (half >> 10) & 0x1fu;
			const uint32 mantissa = half & 0x3ffu;

			uint32 bits;
			if (exponent == 0)
			{
				if (mantissa == 0)
				{
					bits = sign;
				}
				else
				{
					// Subnormal: renormalise.
					uint32 m = mantissa;
					int32 e = -1;
					do
					{
						++e;
						m <<= 1;
					} while ((m & 0x400u) == 0);

					bits = sign | (static_cast<uint32>(127 - 15 - e) << 23) | ((m & 0x3ffu) << 13);
				}
			}
			else if (exponent == 0x1f)
			{
				bits = sign | 0x7f800000u | (mantissa << 13);
			}
			else
			{
				bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
			}

			float result;
			std::memcpy(&result, &bits, sizeof(result));
			return result;
		}

		std::vector<uint8> Downsample(const std::vector<uint8>& source, const uint32 size)
		{
			const uint32 half = size / 2;
			std::vector<uint8> result(half * half * 4);
			for (uint32 y = 0; y < half; ++y)
			{
				for (uint32 x = 0; x < half; ++x)
				{
					for (uint32 c = 0; c < 4; ++c)
					{
						const uint32 sum =
							source[((y * 2) * size + x * 2) * 4 + c] +
							source[((y * 2) * size + x * 2 + 1) * 4 + c] +
							source[((y * 2 + 1) * size + x * 2) * 4 + c] +
							source[((y * 2 + 1) * size + x * 2 + 1) * 4 + c];
						result[(y * half + x) * 4 + c] = static_cast<uint8>((sum + 2) / 4);
					}
				}
			}

			return result;
		}

		bool WriteDxt1Texture(const String& filename, std::vector<uint8> rgba, uint32 size)
		{
			const auto file = AssetRegistry::CreateNewFile(filename);
			if (!file)
			{
				ELOG("Failed to create " << filename);
				return false;
			}

			io::StreamSink sink(*file);

			tex::v1_0::Header header(tex::Version_1_0);
			header.width = static_cast<uint16>(size);
			header.height = static_cast<uint16>(size);
			header.format = tex::v1_0::DXT1;
			header.hasMips = true;

			tex::v1_0::HeaderSaver saver(sink, header);

			for (uint32 mip = 0; mip < header.mipmapOffsets.size() && size >= 4; ++mip)
			{
				std::vector<uint8> compressed(size * size / 2);
				rygCompress(compressed.data(), rgba.data(), static_cast<int>(size), static_cast<int>(size), 0);

				header.mipmapOffsets[mip] = static_cast<uint32>(sink.Position());
				header.mipmapLengths[mip] = static_cast<uint32>(compressed.size());
				sink.Write(reinterpret_cast<const char*>(compressed.data()), compressed.size());

				rgba = Downsample(rgba, size);
				size /= 2;
			}

			saver.finish();
			file->flush();
			return true;
		}

		ScopedVisibleSubtrees::ScopedVisibleSubtrees(SceneNode& root, std::vector<const SceneNode*> keep)
			: m_keep(std::move(keep))
		{
			Hide(root);
		}

		ScopedVisibleSubtrees::~ScopedVisibleSubtrees()
		{
			for (MovableObject* object : m_hidden)
			{
				object->SetVisible(true);
			}
		}

		void ScopedVisibleSubtrees::Hide(SceneNode& node)
		{
			if (std::find(m_keep.begin(), m_keep.end(), &node) != m_keep.end())
			{
				return;
			}

			for (uint32 i = 0; i < node.GetNumAttachedObjects(); ++i)
			{
				// Cameras are never drawn; leave the bake camera (and everyone else's) alone.
				MovableObject* object = node.GetAttachedObject(i);
				if (object && object->IsVisible() && !dynamic_cast<Camera*>(object))
				{
					m_hidden.push_back(object);
					object->SetVisible(false);
				}
			}

			for (uint32 i = 0; i < node.GetNumChildren(); ++i)
			{
				if (auto* child = dynamic_cast<SceneNode*>(node.GetChild(i)))
				{
					Hide(*child);
				}
			}
		}

		ScopedPageLoad::ScopedPageLoad(terrain::Page& page, const uint32 x, const uint32 z, const std::function<bool()>& pumpStreaming)
			: m_page(page)
		{
			// A page the streaming is preparing right now completes on the streaming's main-thread queue;
			// Prepare() would just report it as busy. The parse itself may still be running on the
			// streaming thread, so an idle queue means wait, not done.
			const auto waitStart = std::chrono::steady_clock::now();
			while (page.IsPreparing() && pumpStreaming && std::chrono::steady_clock::now() - waitStart < std::chrono::seconds(30))
			{
				if (!pumpStreaming())
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			}

			if (page.IsPreparing())
			{
				WLOG("Page bake: page " << x << "x" << z << " is still being streamed in, skipped");
				return;
			}

			m_touched = true;
			m_wasPrepared = page.IsPrepared();
			m_wasLoaded = page.IsLoaded();

			if (!m_wasPrepared && !page.Prepare())
			{
				WLOG("Page bake: failed to prepare page " << x << "x" << z);
				return;
			}

			// Load() creates one tile per call.
			while (page.IsLoadable() && !page.Load())
			{
			}

			m_loaded = page.IsLoaded();
			if (!m_loaded)
			{
				WLOG("Page bake: failed to load page " << x << "x" << z);
			}
		}

		ScopedPageLoad::~ScopedPageLoad()
		{
			if (!m_touched)
			{
				return;
			}

			// Leave the page the way the editor's streaming had it.
			if (!m_wasLoaded && m_page.IsLoaded())
			{
				m_page.Unload();
			}

			if (!m_wasPrepared)
			{
				m_page.Destroy();
			}

			// D3D11 only frees released resources once the context is flushed, which presenting a frame
			// does. The command line bake never presents, so without this every page's buffers pile up.
			GraphicsDevice::Get().FlushCommands();
		}
	}
}
