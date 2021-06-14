// Copyright (C) 2021, Robin Klimonow. All rights reserved.

#pragma once


namespace mmo
{
	class LoadingScreen
	{
	public:
		static void Init();
		static void Destroy();
		static void Paint();
		
		static void Show();
		static void Hide();	
	};
	
}
