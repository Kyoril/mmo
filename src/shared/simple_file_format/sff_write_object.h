// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "sff_write_writer.h"

namespace sff
{
	namespace write
	{
		enum
		{
			MultiLine = 1,
			Comma = 2,
			Quoted = 4
		};


		template <class C>
		class Object
		{
		public:

			typedef sff::write::Writer<C> MyWriter;
			typedef unsigned Flags;

		public:

			MyWriter &writer;
			Flags flags;

		public:

			Object(MyWriter &writer, Flags flags)
				: writer(writer)
				, flags(flags)
				, m_parent(nullptr)
				, m_hasElements(false)
			{
				m_hasElements = false;
			}

			Object(Object &parent, Flags flags)
				: writer(parent.writer)
				, flags(flags)
				, m_parent(&parent)
				, m_hasElements(false)
			{
				m_hasElements = false;
				m_parent->beforeElement();
			}

			bool usesMultiLine() const
			{
				return (flags & MultiLine) != 0;
			}

			bool usesComma() const
			{
				return (flags & Comma) != 0;
			}

			void Finish()
			{
				if (m_parent)
				{
					m_parent->afterElement();
				}

				if (usesMultiLine() &&
				        hasParent())
				{
					writer.leaveLevel();
					writer.newLine();
					writer.writeIndentation();
				}
			}

			void beforeElement()
			{
				if (hasElements())
				{
					if (usesComma())
					{
						writer.writeComma();
					}
				}

				if (usesMultiLine())
				{
					if (hasParent() ||
					        hasElements())
					{
						writer.newLine();
						writer.writeIndentation();
					}
				}
				else if (hasElements())
				{
					writer.space();
				}
			}

			void afterElement()
			{
				m_hasElements = true;
			}

			bool hasElements() const
			{
				return m_hasElements;
			}

			bool hasParent() const
			{
				return (m_parent != 0);
			}

		private:

			Object *m_parent;
			bool m_hasElements;
		};
	}
}
