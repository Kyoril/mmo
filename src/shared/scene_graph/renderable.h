// Copyright (C) 2021, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// Interface for a renderable object in a scene.
	class Renderable
	{
	public:
		class Visitor
        {
        public:
            /// Virtual destructor needed as class has virtual methods.
            virtual ~Visitor() = default;

		public:
            /// Generic visitor method. 
            /// @param rend The Renderable instance being visited
            /// @param lodIndex The LOD index to which this Renderable belongs. Some
            ///                 objects support LOD and this will tell you whether the Renderable
            ///                 you're looking at is from the top LOD (0) or otherwise
            /// @param isDebug Whether this is a debug renderable or not.
            virtual void Visit(Renderable& rend, uint16 lodIndex, bool isDebug) = 0;
        };

	public:
		virtual ~Renderable() = default;

	public:

	};
}
