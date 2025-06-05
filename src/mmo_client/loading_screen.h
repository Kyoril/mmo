// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/signal.h"
#include "base/typedefs.h"

namespace mmo
{
	class LoadingScreen
	{
	public:
		static signal<void()> LoadingScreenShown;

	public:
		static void Init();

		static void Destroy();

		static void SetLoadingScreenTexture(const String& texture);

		static void Paint();
		
		static void Show();

		static void Hide();
	};
	
}
