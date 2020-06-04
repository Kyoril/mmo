// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include <Windows.h>


namespace mmo
{
	/// This class manages the main window of the application.
	class MainWindow final : public NonCopyable
	{
	public:
		explicit MainWindow();

	private:
		/// Ensures that the window class has been created by creating it if needed.
		void EnsureWindowClassCreated();
		/// Creates the internal window handle.
		void CreateWindowHandle();

	private:
		/// Static window message callback procedure. Simply tries to route the message to the
		/// window instance.
		static LRESULT CALLBACK WindowMsgProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam);
		/// Instanced window callback procedure.
		LRESULT CALLBACK MsgProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam);

	private:
		HWND m_windowHandle;

		HWND m_statusBarHandle;
	};
}
