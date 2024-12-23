// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "sff_write_object.h"

namespace sff
{
	namespace write
	{
		template <class C>
		struct Array : Object<C>
		{
			typedef Object<C> Super;
			typedef typename Super::Flags Flags;


			template <class Name>
			Array(Object<C> &parent, const Name &name, Flags flags)
				: Super(parent, flags)
			{
				this->writer.writeKey(name);

				if (this->usesMultiLine())
				{
					this->writer.newLine();
					this->writer.writeIndentation();
					this->writer.enterLevel();
				}

				this->writer.enterArray();
			}

			Array(Array<C> &parent, Flags flags)
				: Super(parent, flags)
			{
				if (this->usesMultiLine())
				{
					this->writer.enterLevel();
				}

				this->writer.enterArray();
			}

			bool isQuoted() const
			{
				return (this->flags & Quoted) != 0;
			}

			template <class Value>
			void addElement(const Value &value)
			{
				this->beforeElement();
				this->writer.writeValue(value);
				this->afterElement();
			}

			void Finish()
			{
				Super::Finish();
				if (this->hasParent())
				{
					this->writer.leaveArray();
				}
			}
		};
	}
}
