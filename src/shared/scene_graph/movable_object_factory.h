// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	class Scene;
	class MovableObject;

	/// Base type definition of a factory class which produces a certain kind of MovableObject.
	class MovableObjectFactory
	{
	protected:
		/// Type flags.
		uint32 m_typeFlags = 0xffffffff;

		/// Internal implementation of the create method, must be overridden.
		virtual MovableObject* CreateInstanceImpl(const String& name) = 0;

	public:
		/// Default constructor.
		MovableObjectFactory() = default;

		/// Virtual default destructor because of inheritance.
		virtual ~MovableObjectFactory() = default;

	public:
		/// Gets the type identifier of this factory. Must be overridden.
		[[nodiscard]] virtual const String& GetType() const = 0;

		/// Creates a new instance of a movable object.
		///	@param name Name of the new object to create.
		///	@param scene Scene where the object belongs to.
		virtual MovableObject* CreateInstance(const String& name, Scene& scene);

		/// Destroys a movable object that was created by this factory. Must be overridden.
		virtual void DestroyInstance(MovableObject& object) = 0;
		
		[[nodiscard]] virtual bool RequestTypeFlags() const noexcept { return false; }

		void SetTypeFlags(const uint32 flags) noexcept { m_typeFlags = flags; }

		[[nodiscard]] uint32 GetTypeFlags() const noexcept { return m_typeFlags; }
	};
}
