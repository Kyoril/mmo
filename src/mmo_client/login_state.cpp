// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "login_state.h"

#include "base/macros.h"
#include "log/default_log_levels.h"

#if 0
#include "tex/pre_header.h"
#include "tex/pre_header_load.h"
#include "tex_v1_0/header.h"
#include "tex_v1_0/header_load.h"

#include "binary_io/stream_source.h"
#include "binary_io/reader.h"

#include "archive.h"
#include "hpak_archive.h"
#endif

namespace mmo
{
	const std::string LoginState::Name = "login";


	void LoginState::OnEnter()
	{
		// Register drawing of the login ui
		m_paintLayer = Screen::AddLayer(std::bind(&LoginState::OnPaint, this), 1.0f, 0);

		// Create geometry to render the logo on screen


#if 0
		// Load texture to render
		std::unique_ptr<IArchive> archive = std::make_unique<HPAKArchive>("./Data/Interface.hpak");
		archive->Load();

		// Try to open up a file
		auto file = archive->Open("Logo.htex");

		io::StreamSource source{ *file };
		io::Reader reader{ source };

		tex::PreHeader preHeader;
		if (!tex::loadPreHeader(preHeader, reader))
		{
			ASSERT(!"Failed to load texture pre header");
		}

		tex::v1_0::Header header(tex::Version_1_0);
		if (!tex::v1_0::loadHeader(header, reader))
		{
			ASSERT(!"Failed to load texture header");
		}

		DLOG("Loaded texture. Size: " << header.width << "x" << header.height << " (Has Mips: " << header.hasMips << ")");

		// Unload archive
		archive->Unload();
#endif
	}

	void LoginState::OnLeave()
	{
		Screen::RemoveLayer(m_paintLayer);
	}

	const std::string & LoginState::GetName() const
	{
		return LoginState::Name;
	}

	void LoginState::OnPaint()
	{
		// Setup something to paint

	}
}
