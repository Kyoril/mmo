// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "login_state.h"
#include "assets/asset_registry.h"

#include "base/macros.h"
#include "log/default_log_levels.h"

#include "tex/pre_header.h"
#include "tex/pre_header_load.h"
#include "tex_v1_0/header.h"
#include "tex_v1_0/header_load.h"
#include "binary_io/stream_source.h"
#include "binary_io/reader.h"

#include "asio.hpp"


namespace mmo
{
	const std::string LoginState::Name = "login";


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

		// Register drawing of the login ui
		m_paintLayer = Screen::AddLayer(std::bind(&LoginState::OnPaint, this), 1.0f, ScreenLayerFlags::IdentityTransform);
	}

	void LoginState::OnLeave()
	{
		// No longer draw current layer
		Screen::RemoveLayer(m_paintLayer);

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
	}
}
