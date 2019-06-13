// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "login_state.h"
#include "assets/asset_registry.h"

#include "base/macros.h"
#include "base/utilities.h"
#include "log/default_log_levels.h"

#include "asio.hpp"

#include <set>


namespace mmo
{
	const std::string LoginState::Name = "login";


	// Forward declaration for detail methods
	void LoadUIFile(const std::string& filename);
	
	namespace detail
	{
		// TODO: Implement the s_tocFiles check in the AssetRegistry system somehow?
		// This could be useful to prevent loading files twice which seems like a common
		// thing to do for file loading.

		/// A list of files that have been loaded.
		static std::set<std::string> s_tocFiles;

		/// Subroutine for loading a *.lua file for the frame ui.
		/// @param file A file pointer for reading the file contents.
		/// @param filename Name of the file in case it is needed.
		static void LoadFrameScript(std::unique_ptr<std::istream> file, const std::string& filename)
		{
			ASSERT(file);

			// TODO: Load (and execute) lua script
		}

		/// Subroutine for loading a *.xml file for the frame ui. An xml file describes frames to
		/// be created, but can also reference script files directly.
		/// @param file A file pointer for reading the file contents.
		/// @param filename Name of the file in case it is needed.
		static void LoadFrameXML(std::unique_ptr<std::istream> file, const std::string& filename)
		{
			ASSERT(file);

			// TODO: Load and parse xml file
		}

		/// Subroutine for loading a *.toc file for the frame ui. A toc file is a text file which
		/// contains file names (one per line), which are then loaded.
		/// @param file A file pointer for reading the file contents.
		/// @param filename Name of the file in case it is needed.
		static void LoadTOCFile(std::unique_ptr<std::istream> file, const std::string& filename)
		{
			ASSERT(file);

			// Read each line of the file
			std::string line;
			while (std::getline(*file, line))
			{
				// Check for empty lines or comments
				if (line.empty())
					continue;

				// Remove all occurences of \r character as getline only accounts for \n and on
				// windows, lines might end with \r\n
				size_t index = 0;
				while ((index = line.find('\r', index)) != line.npos)
				{
					line.erase(index, 1);
				}

				// Load the ui file
				LoadUIFile(line);
			}
		}

		/// This method checks if the given file name is already flagged as being loaded.
		/// If it isn't loaded, it also flags this filename.
		/// @return true if the file wasn't flagged, so loading can proceed.
		static bool LoadCycleCheck(const std::string& filename)
		{
			// Prevent a load cycle and double load
			if (s_tocFiles.find(filename) != s_tocFiles.end())
				return false;

			// Add to the set
			s_tocFiles.insert(filename);
			return true;
		}
	}
	
	/// Loads a UI file, which can be one of: *.toc, *.xml or *.lua. The respective file is
	/// then handled properly. The file is loaded using the AssetRegistry system.
	/// @param filename Name of the file to load.
	void LoadUIFile(const std::string& filename)
	{
		if (!detail::LoadCycleCheck(filename))
			return;

		// Extract the file extension
		std::string extension = GetFileExtension(filename);
		std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

		// Try to open the file
		auto file = AssetRegistry::OpenFile(filename);
		ASSERT(file);

		// Call the respective subroutine depending on the file extension
		if (extension == ".toc")
		{
			detail::LoadTOCFile(std::move(file), filename);
		}
		else if (extension == ".lua")
		{
			detail::LoadFrameScript(std::move(file), filename);
		}
		else if (extension == ".xml")
		{
			detail::LoadFrameXML(std::move(file), filename);
		}
	}

	void LoginState::OnEnter()
	{
		// Vertex position
		const float w = 512.0f;
		const float h = 256.0f;
		const float x = 512.0f - w / 2.0f;
		const float y = 100.0f;

		// Create vertex buffer
		const POS_COL_TEX_VERTEX vertices[] = {
			{ { x, y + h, 0.0f }, 0xffffffff, { 0.0f, 0.0f } },
			{ { x, y, 0.0f }, 0xffffffff, { 0.0f, 1.0f } },
			{ { x + w, y, 0.0f }, 0xffffffff, { 1.0f, 1.0f } },
			{ { x + w, y + h, 0.0f }, 0xffffffff, { 1.0f, 0.0f } }
		};
		m_vertexBuffer = GraphicsDevice::Get().CreateVertexBuffer(4, sizeof(POS_COL_TEX_VERTEX), false, vertices);

		// Create index buffer
		const uint16 indices[] = {
			0, 1, 2,
			2, 3, 0
		};
		m_indexBuffer = GraphicsDevice::Get().CreateIndexBuffer(6, IndexBufferSize::Index_16, indices);

		// Try to load interface.hpak
		std::unique_ptr<std::istream> file = AssetRegistry::OpenFile("Interface/Logo.htex");
		if (file == nullptr)
		{
			throw std::runtime_error("Failed to load logo texture");
		}

		// Load the logo texture
		m_texture = GraphicsDevice::Get().CreateTexture();
		m_texture->Load(file);

		// Clear all loaded toc files
		detail::s_tocFiles.clear();

		// Load the root toc file
		LoadUIFile("Interface/GlueUI/GlueUI.toc");

		// Make the logo frame element
		m_logoFrame = std::make_shared<Frame>("LoginLogo");

		// Register drawing of the login ui
		m_paintLayer = Screen::AddLayer(std::bind(&LoginState::OnPaint, this), 1.0f, ScreenLayerFlags::IdentityTransform);
	}

	void LoginState::OnLeave()
	{
		// No longer draw current layer
		Screen::RemoveLayer(m_paintLayer);

		// Reset the logo frame ui
		m_logoFrame = nullptr;

		// Reset texture
		m_texture.reset();

		// Destroy vertex and index buffer
		m_indexBuffer.reset();
		m_vertexBuffer.reset();
	}

	const std::string & LoginState::GetName() const
	{
		return LoginState::Name;
	}

	void LoginState::OnPaint()
	{
		auto& gx = GraphicsDevice::Get();

		// Setup orthographic projection
		gx.SetTransformMatrix(TransformType::Projection, Matrix4::MakeOrthographic(0.0f, 1024.0f, 1024.0f, 0.0f, 0.0f, 100.0f));

		// Setup render states to draw
		gx.SetVertexFormat(VertexFormat::PosColorTex1);
		gx.SetTopologyType(TopologyType::TriangleList);

		// Bind texture
		gx.BindTexture(m_texture, ShaderType::PixelShader, 0);

		// Setup geometry buffers
		m_vertexBuffer->Set();
		m_indexBuffer->Set();

		// Draw indexed
		gx.DrawIndexed();

		// Render the logo frame ui
		m_logoFrame->Render();
	}
}
