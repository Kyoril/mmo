// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics/material.h"
#include "base/typedefs.h"
#include "graphics/graphics_device.h"

namespace mmo
{
	class Camera;
	class Scene;
	class RenderOperation;

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

        /// @brief Called just before the renderable is being rendered.
        /// @param scene The scene that this renderable belongs to.
        /// @param graphicsDevice The graphics device that is used for rendering.
		/// @param camera The camera that is used for rendering.
        /// @return true if the automatic rendering should proceed, false to skip rendering.
        virtual bool PreRender(Scene& scene, GraphicsDevice& graphicsDevice, Camera& camera)
		{
            return true;
		}

        /// @brief Called immediately after the renderable has been rendered.
        /// @param scene The scene that this renderable belongs to.
        /// @param graphicsDevice The graphics device that was used for rendering.
		/// @param camera The camera that was used for rendering.
        virtual void PostRender(Scene& scene, GraphicsDevice& graphicsDevice, Camera& camera)
        {
        }

        /// @brief Gets the render operation which tells the engine how this renderable should be rendered.
        /// @param operation The render operation to setup.
        virtual void PrepareRenderOperation(RenderOperation& operation) = 0;

		[[nodiscard]] virtual const Matrix4& GetWorldTransform() const = 0;

		[[nodiscard]] virtual float GetSquaredViewDepth(const Camera& camera) const = 0;
        
        [[nodiscard]] virtual bool GetCastsShadows() const { return false; }
        
        [[nodiscard]] virtual MaterialPtr GetMaterial() const = 0;
	};
}
