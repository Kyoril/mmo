// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "login_state.h"
#include "asset_registry.h"

#include "base/macros.h"
#include "log/default_log_levels.h"

#include "tex/pre_header.h"
#include "tex/pre_header_load.h"
#include "tex_v1_0/header.h"
#include "tex_v1_0/header_load.h"
#include "binary_io/stream_source.h"
#include "binary_io/reader.h"


namespace mmo
{
	const std::string LoginState::Name = "login";


	void LoginState::OnEnter()
	{
		// Create vertex buffer
		const POS_COL_TEX_VERTEX vertices[] = {
			{ { -1.0f, -1.0f, 0.0f }, 0xffffffff, { 0.0f, 0.0f } },
			{ { -1.0f,  1.0f, 0.0f }, 0xffffffff, { 0.0f, 1.0f } },
			{ {  1.0f,  1.0f, 0.0f }, 0xffffffff, { 1.0f, 1.0f } },
			{ {  1.0f, -1.0f, 0.0f }, 0xffffffff, { 1.0f, 0.0f } }
		};
		m_vertexBuffer = GraphicsDevice::Get().CreateVertexBuffer(4, sizeof(POS_COL_TEX_VERTEX), false, vertices);

		// Create index buffer
		const uint16 indices[] = {
			0, 1, 2,
			2, 3, 0
		};
		m_indexBuffer = GraphicsDevice::Get().CreateIndexBuffer(4, IndexBufferSize::Index_16, indices);

		// Register drawing of the login ui
		m_paintLayer = Screen::AddLayer(std::bind(&LoginState::OnPaint, this), 1.0f, 0);

		// Try to load interface.hpak
		std::unique_ptr<std::istream> file = AssetRegistry::OpenFile("Interface/Logo.htex");
		if (file == nullptr)
		{
			throw std::runtime_error("Failed to load logo texture");
		}

		io::StreamSource source{ *file };
		io::Reader reader{ source };

		tex::PreHeader preHeader;
		if (!tex::loadPreHeader(preHeader, reader))
		{
			throw std::runtime_error("Failed to load texture pre header");
		}

		tex::v1_0::Header header(tex::Version_1_0);
		if (!tex::v1_0::loadHeader(header, reader))
		{
			throw std::runtime_error("Failed to load texture header");
		}

		DLOG("Loaded logo texture. Size: " << header.width << "x" << header.height << " (Has Mips: " << header.hasMips << ")");
	}

	void LoginState::OnLeave()
	{
		// No longer draw current layer
		Screen::RemoveLayer(m_paintLayer);

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
		// Setup render states to draw
		GraphicsDevice::Get().SetVertexFormat(VertexFormat::PosColorTex1);
		GraphicsDevice::Get().SetTopologyType(TopologyType::TriangleList);

		// TODO: Setup texture

		// Setup geometry buffers
		m_vertexBuffer->Set();
		m_indexBuffer->Set();

		// Draw indexed
		GraphicsDevice::Get().DrawIndexed();
	}
}
