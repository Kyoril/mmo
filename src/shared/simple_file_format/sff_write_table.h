// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "sff_write_object.h"
#include "sff_write_array.h"

namespace sff
{
	namespace write
	{
		template <class C>
		struct Table : Object<C>
		{
			typedef Object<C> Super;
			typedef typename Super::MyWriter MyWriter;
			typedef typename Super::Flags Flags;


			template <class Name>
			Table(Object<C> &parent, const Name &name, Flags flags)
				: Super(parent, flags)
			{
				this->writer.writeKey(name);

				if (this->usesMultiLine())
				{
					this->writer.newLine();
					this->writer.writeIndentation();
					this->writer.enterLevel();
				}

				this->writer.enterTable();
			}

			Table(Array<C> &parent, Flags flags)
				: Super(parent, flags)
			{
				if (this->usesMultiLine())
				{
					this->writer.enterLevel();
				}

				this->writer.enterTable();
			}

			//global table
			Table(MyWriter &writer, Flags flags)
				: Super(writer, flags)
			{
			}

			template <class Name, class Value>
			void addKey(const Name &name, const Value &value)
			{
				this->beforeElement();
				this->writer.writeKey(name);
				this->writer.writeValue(value);
				this->afterElement();
			}

			void Finish()
			{
				Super::Finish();
				if (this->hasParent())
				{
					this->writer.leaveTable();
				}
			}
		};
	}
}
