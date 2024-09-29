// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "console.h"
#if defined(WIN32) || defined(_WIN32)
#	include <windows.h>
#	include <conio.h>
#elif defined(__APPLE__)
#   include <iostream>
#   include <unistd.h>
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
#elif defined(__APPLE__)
        switch(id)
        {
            case Console::Black:
                std::cout << "\033[30m";
                break;
                
            case Console::DarkBlue:
            case Console::Blue:
                std::cout << "\033[34m";
                break;
                
            case Console::DarkGreen:
            case Console::Green:
                std::cout << "\033[32m";
                break;
                
            case Console::DarkCyan:
            case Console::Cyan:
                std::cout << "\033[36m";
                break;
                
            case Console::DarkRed:
            case Console::Red:
            case Console::DarkBrown:
                std::cout << "\033[31m";
                break;
                
            case Console::DarkMagenta:
            case Console::Magenta:
                std::cout << "\033[35m";
                break;
                
            case Console::LightGray:
            case Console::DarkGray:
                std::cout << "\033[37m";
                break;
                
            case Console::Yellow:
                std::cout << "\033[33m";
                break;
                
            default:
                std::cout << "\033[0m";
                break;
        }
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
