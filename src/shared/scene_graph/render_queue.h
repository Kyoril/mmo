#pragma once

namespace mmo
{
	enum RenderQueueGroupId
	{
        /// Use this queue for objects which must be rendered first e.g. backgrounds
		Background = 0,
		
        /// First queue (after backgrounds), used for skies if rendered first
		SkiesEarly = 5,
		Queue1 = 10,
		Queue2 = 20,
		WorldGeometry1 = 25,
		Queue3 = 30,
		Queue4 = 40,
		
		/// The default render queue
		Main = 50,
		Queue6 = 60,
		Queue7 = 70,
		WorldGeometry2 = 75,
		Queue8 = 80,
		Queue9 = 90,
		
        /// Penultimate queue (before overlays), used for skies if rendered last
		SkiesLate = 95,
		
        /// Use this queue for objects which must be rendered last e.g. overlays
		Overlay = 100,
		
		/// Final possible render queue, don't exceed this
		Max = 105
	};
}