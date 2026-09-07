// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

namespace mmo
{
	/// Relevance bands an action can be pushed at. The engine always runs the highest-relevance
	/// action it can, so these are the whole priority system - a strategy expresses "this matters
	/// more than that" by picking a band, and offsets from it for ordering within the band.
	///
	/// The bands are deliberately far apart. An offset of a few points stays inside its band,
	/// which means a strategy can be written without knowing what every other strategy pushes.
	namespace bot_relevance
	{
		enum Type
		{
			/// Filler. Runs only when the bot has nothing else to do at all.
			Idle = 0,

			/// The default band for a strategy's standing actions.
			Default = 5,

			/// Ordinary business: attacking the current target, picking the next one.
			Normal = 10,

			/// Wants to happen before ordinary business, but is not urgent.
			High = 20,

			/// Getting into position. Above High because an action that cannot reach its target
			/// is useless until movement has run.
			Move = 30,

			/// Reacting to something the server just told us, such as a swing that failed
			/// because we were facing the wrong way.
			Interrupt = 40,

			/// Survival. Nothing outranks this.
			Emergency = 90,
		};
	}
}
