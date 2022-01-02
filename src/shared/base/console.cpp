// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "console.h"
#if defined(WIN32) || defined(_WIN32)
#	include <windows.h>
#	include <conio.h>
#endif

namespace mmo
{
	Console::Color Console::getTextColor()
	{
#if defined(WIN32) || defined(_WIN32)
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
		return static_cast<Color>(csbi.wAttributes & 0x0f);
#else
		return White;
#endif
	}

	Console::Color Console::getBackgroundColor()
	{
#if defined(WIN32) || defined(_WIN32)
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
		return static_cast<Color>((csbi.wAttributes >> 4) & 0x0f);
#else
		return Black;
#endif
	}

	void Console::setTextColor(Color id)
	{
#if defined(WIN32) || defined(_WIN32)
		SetConsoleTextAttribute(
		    GetStdHandle(STD_OUTPUT_HANDLE),
		    static_cast<WORD>(id | (getBackgroundColor() << 4)));
#else
		(void)id;
#endif
	}

	void Console::setBackgroundColor(Color id)
	{
#if defined(WIN32) || defined(_WIN32)
		SetConsoleTextAttribute(
		    GetStdHandle(STD_OUTPUT_HANDLE),
		    static_cast<WORD>(getTextColor() | (id << 4)));
#else
		(void)id;
#endif
	}
}
