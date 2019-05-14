// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "console.h"
#include "console_commands.h"
#include "event_loop.h"

#include "base/filesystem.h"
#include "log/default_log_levels.h"
#include "graphics/graphics_device.h"


namespace mmo
{
	std::map<std::string, Console::ConsoleCommand, StrCaseIComp> Console::s_consoleCommands;

	static VertexBufferPtr s_consoleVertBuf;
	static IndexBufferPtr s_consoleIndBuf;

	static scoped_connection s_consolePaintCon;
	
	void Console::Initialize(const std::string& configFile)
	{
		// Ensure the folder is created
		std::filesystem::path folder = configFile;
		std::filesystem::create_directories(folder.parent_path());

		// Register some default console commands
		RegisterCommand("ver", console_commands::ConsoleCommand_Ver, ConsoleCommandCategory::Default, "Displays the client version.");
		RegisterCommand("run", console_commands::ConsoleCommand_Run, ConsoleCommandCategory::Default, "Runs a console script.");

		// Load the config file
		console_commands::ConsoleCommand_Run("run", configFile);

		// Initialize the graphics api
		auto& device = GraphicsDevice::CreateD3D11();
		device.SetWindowTitle("MMORPG");

		// Setup vertex buffer
		s_consoleVertBuf = GraphicsDevice::Get().CreateVertexBuffer(4, sizeof(POS_COL_VERTEX), true, nullptr);

		// Setup index buffer
		const uint16 s_consoleIndices[] = {
			0, 1, 2,
			2, 3, 0
		};
		s_consoleIndBuf = GraphicsDevice::Get().CreateIndexBuffer(6, IndexBufferSize::Index_16, s_consoleIndices);

		// Register paint command
		s_consolePaintCon = EventLoop::Paint.connect(&Console::Paint);
	}

	void Console::Destroy()
	{
		// Remove paint command
		s_consolePaintCon.disconnect();

		// Reset buffers
		s_consoleIndBuf.reset();
		s_consoleVertBuf.reset();

		UnregisterCommand("run");
		UnregisterCommand("ver");
	}

	inline void Console::RegisterCommand(const std::string & command, ConsoleCommandHandler handler, ConsoleCommandCategory category, const std::string & help)
	{
		// Don't do anything if this console command is already registered
		auto it = s_consoleCommands.find(command);
		if (it != s_consoleCommands.end())
		{
			return;
		}

		// Build command structure and add it
		ConsoleCommand cmd;
		cmd.category = category;
		cmd.help = std::move(help);
		cmd.handler = std::move(handler);
		s_consoleCommands.emplace(command, cmd);
	}

	inline void Console::UnregisterCommand(const std::string & command)
	{
		// Remove the respective iterator
		auto it = s_consoleCommands.find(command);
		if (it != s_consoleCommands.end())
		{
			s_consoleCommands.erase(it);
		}
	}

	void Console::ExecuteCommand(std::string commandLine)
	{
		// Will hold the command name
		std::string command;
		std::string arguments;

		// Find the first space and use it to get the command
		auto space = commandLine.find(' ');
		if (space == commandLine.npos)
		{
			command = commandLine;
		}
		else
		{
			command = commandLine.substr(0, space);
			arguments = commandLine.substr(space + 1);
		}

		// If somehow the command is empty, just stop here without saying anything.
		if (command.empty())
		{
			return;
		}

		// Check if such argument exists
		auto it = s_consoleCommands.find(command);
		if (it == s_consoleCommands.end())
		{
			ELOG("Unknown console command \"" << command << "\"");
			return;
		}

		// Now execute the console commands handler if there is any
		if (it->second.handler)
		{
			it->second.handler(command, arguments);
		}
	}

	void Console::Paint()
	{
		auto& gx = GraphicsDevice::Get();

		// Activate alpha blending
		gx.SetBlendMode(BlendMode::Alpha);

		// Setup render states
		gx.SetTopologyType(TopologyType::TriangleStrip);
		gx.SetVertexFormat(VertexFormat::PosColor);
		gx.SetTransformMatrix(TransformType::World, Matrix4::Identity);
		gx.SetTransformMatrix(TransformType::View, Matrix4::Identity);
		gx.SetTransformMatrix(TransformType::Projection, Matrix4::Identity);

		// Update vertex buffer contents
		{
			CScopedGxBufferLock<POS_COL_VERTEX> lock{ *s_consoleVertBuf };
			lock[0]->pos[0] = -1.0f; lock[0]->pos[1] = 0.0f; lock[0]->pos[2] = 0.0f;
			lock[0]->color = 0xA0000000;
			lock[1]->pos[0] = -1.0f; lock[1]->pos[1] = 1.0f; lock[1]->pos[2] = 0.0f;
			lock[1]->color = 0xA0000000;
			lock[2]->pos[0] = 1.0f; lock[2]->pos[1] = 1.0f; lock[2]->pos[2] = 0.0f;
			lock[2]->color = 0xA0000000;
			lock[3]->pos[0] = 1.0f; lock[3]->pos[1] = 0.0f; lock[3]->pos[2] = 0.0f;
			lock[3]->color = 0xA0000000;
		}

		// Draw the geometry
		s_consoleVertBuf->Set();
		s_consoleIndBuf->Set();

		// Draw console
		gx.DrawIndexed();
	}
}
