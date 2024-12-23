// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "sff_read_parser.h"
#include "sff_read_token.h"

#include <optional>
#include <memory>
#include <vector>
#include <map>
#include <cstddef>
#include <cassert>
#include <limits>

namespace sff
{
	namespace read
	{
		namespace tree
		{
			template <class T>
			class Object
			{
			public:

				typedef Parser<T> MyParser;
				typedef typename MyParser::String MyString;
				typedef Token<T> MyToken;

			public:

				virtual ~Object()
				{
				}

				virtual MyString GetContent() const
				{
					return MyString();
				}
			};


			template <class T>
			class Integer : public Object<T>
			{
			public:

				typedef Object<T> Super;
				typedef typename Super::MyToken MyToken;
				typedef typename Super::MyString MyString;

			public:

				bool negative;
				MyToken value;

			public:

				explicit Integer(bool negative, const MyToken &value)
					: negative(negative)
					, value(value)
				{
					assert((value.begin == value.begin) && (value.end == value.end));
					assert(true || MyString(value.begin, value.end).size());
				}

				virtual MyString GetContent() const
				{
					return value.str();
				}

				template <class TValue>
				TValue getValue() const
				{
					TValue temp = value.template toInteger<TValue>();
					SignMaker < TValue,
					          std::is_floating_point<TValue>::value ||
					          std::numeric_limits<TValue>::is_signed
					          > ()(temp, *this);
					return temp;
				}

			private:

				template <class Value, bool Signed>
				struct SignMaker
				{
					void operator ()(Value &value, const Integer &object);
				};

				template <class Value>
				struct SignMaker<Value, true>
				{
					void operator ()(Value &number, const Integer &object)
					{
						if (object.negative)
						{
							number = -number;
						}
					}
				};

				template <class Value>
				struct SignMaker<Value, false>
				{
					void operator ()(Value &/*number*/, const Integer &object)
					{
						if (object.negative)
						{
							throw NegativeUnsignedException<T>(object.value);
						}
					}
				};
			};


			template <class T>
			class String : public Object<T>
			{
			public:

				typedef Object<T> Super;
				typedef typename Super::MyToken MyToken;
				typedef typename Super::MyString MyString;

			public:

				MyString content;

			public:

				explicit String(const MyString &content)
					: content(content)
				{
				}

				virtual MyString GetContent() const
				{
					return content;
				}
			};


			template <class T, class P>
			Object<T> *parseObject(P &parser, DataType type);

			template <class T>
			class Table;


			template <class T>
			class Array : public Object<T>
			{
			public:

				typedef Object<T> Super;
				typedef typename Super::MyToken MyToken;
				typedef typename Super::MyParser MyParser;
				typedef Object<T> Element;
				typedef std::vector<std::shared_ptr<Element>> Elements;
				typedef std::size_t Index;
				typedef typename Super::MyString MyString;

			public:

				Elements elements;

			public:

				Index getSize() const
				{
					return elements.size();
				}

				const Element &getElement(Index index) const
				{
					return *elements.at(index);
				}

				const Table<T> *getTable(Index index) const;

				const Array<T> *getArray(Index index) const
				{
					return dynamic_cast<const Array<T> *>(&getElement(index));
				}

				bool tryGetString(Index index, MyString &value) const
				{
					const String<T> *const element = dynamic_cast<const String<T> *>(&getElement(index));

					if (element)
					{
						value = element->content.str();
						return true;
					}

					return false;
				}

				MyString getString(Index index, const MyString &default_) const
				{
					const String<T> *const element = dynamic_cast<const String<T> *>(&getElement(index));

					if (element)
					{
						return element->content;
					}

					return default_;
				}

				MyString getString(Index index) const
				{
					return getString(index, MyString());
				}

				template <class TValue>
				TValue getInteger(Index index, TValue default_) const
				{
					TValue temp;
					if (findInteger(index, temp))
					{
						return temp;
					}
					else
					{
						return default_;
					}
				}

				template <class Integer>
				std::optional<Integer> getOptionalInt(Index index) const
				{
					Integer temp;
					if (findInteger(index, temp))
					{
						return temp;
					}
					return {};
				}

				template <class TValue>
				bool findInteger(Index index, TValue &out_value) const
				{
					const Integer<T> *const element = dynamic_cast<const Integer<T> *>(&getElement(index));

					if (element)
					{
						out_value = element->template getValue<TValue>();
						return true;
					}

					return false;
				}

				void parse(MyParser &parser)
				{
					parser.enterArrayEx();

					DataType type;

					while (parser.detectDataType(type))
					{
						elements.emplace_back(std::shared_ptr<Element>(parseObject<T>(parser, type)));
						parser.skipOptionalComma();
					}

					parser.leaveArrayEx();
				}
			};


			template <class T>
			class Table : public Object<T>
			{
			public:

				typedef Object<T> Super;
				typedef typename Super::MyToken MyToken;
				typedef typename Super::MyString MyString;
				typedef typename Super::MyParser MyParser;
				typedef Object<T> Element;
				typedef std::map<MyString, std::shared_ptr<Element>> Members;
				//typedef boost::ptr_map<MyString, Element> Members;

			public:

				Members members;

			public:

				const Element *getElement(const MyString &name) const
				{
					const typename Members::const_iterator i = members.find(name);

					if (i != members.end())
					{
						return (i->second.get());
					}

					return nullptr;
				}

				const Table<T> *getTable(const MyString &name) const
				{
					return dynamic_cast<const Table<T> *>(getElement(name));
				}

				const Array<T> *getArray(const MyString &name) const
				{
					return dynamic_cast<const Array<T> *>(getElement(name));
				}

				bool tryGetString(const MyString &name, MyString &value) const
				{
					const String<T> *const element = dynamic_cast<const String<T> *>(getElement(name));

					if (element)
					{
						value = element->content;
						return true;
					}
					else
					{
						return false;
					}
				}

				MyString getString(const MyString &name, const MyString &default_) const
				{
					const String<T> *const element = dynamic_cast<const String<T> *>(getElement(name));

					if (element)
					{
						return element->content;
					}

					return default_;
				}

				MyString getString(const MyString &name) const
				{
					return getString(name, MyString());
				}

				std::optional<MyString> getOptionalString(const MyString &name) const
				{
					MyString result;
					if (tryGetString(name, result))
					{
						return result;
					}
					return {};
				}

				template <class TValue>
				TValue getInteger(const MyString &name, TValue default_) const
				{
					const Integer<T> *const element = dynamic_cast<const Integer<T> *>(getElement(name));

					if (element)
					{
						return element->template getValue<TValue>();
					}

					return default_;
				}

				template <class TValue>
				bool tryGetInteger(const MyString &name, TValue &out_value) const
				{
					const Integer<T> *const element = dynamic_cast<const Integer<T> *>(getElement(name));

					if (element)
					{
						out_value = element->template getValue<TValue>();
						return true;
					}

					return false;
				}

				void parse(MyParser &parser, bool isGlobal)
				{
					if (!isGlobal)
					{
						parser.enterTableEx();
					}

					MyString key;

					while (parser.parseAssignment(key))
					{
						const DataType type = parser.detectDataTypeEx();
						std::shared_ptr<Object<T> > element(parseObject<T>(parser, type));
						members[key] = std::move(element);
						parser.skipOptionalComma();
					}

					if (isGlobal)
					{
						//must be at the end of file
						parser.expectEndOfFile();
					}
					else
					{
						parser.skipOptionalComma();
						parser.leaveTableEx();
					}
				}

				void parseFile(MyParser &parser)
				{
					parse(parser, true);
				}
			};


			template <class T>
			const Table<T> *Array<T>::getTable(Index index) const
			{
				return dynamic_cast<const Table<T> *>(&getElement(index));
			}

			template <class T, class P>
			Object<T> *parseObject(P &parser, DataType type)
			{
				typedef Token<T> MyToken;

				std::unique_ptr<Object<T> > value;

				switch (type)
				{
				case TypeInteger:
					{
						bool isNegative;
						MyToken digits;

						parser.parseIntegerTokenEx(true, isNegative, digits);
						value.reset(new Integer<T>(isNegative, digits));
						break;
					}

				case TypeString:
					{
						const MyToken content = parser.parseStringTokenEx();
						value.reset(new tree::String<T>(
						                parser.decodeStringLiteral(content)));
						break;
					}

				case TypeArray:
					value.reset(new Array<T>());
					static_cast<Array<T> &>(*value).parse(parser);
					break;

				case TypeTable:
					value.reset(new Table<T>());
					static_cast<Table<T> &>(*value).parse(parser, false);
					break;
				}

				return value.release();
			}
		}
	}
}
