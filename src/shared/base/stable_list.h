/////////////////////////////////////////////////////////////////////////////////////
// simple - lightweight & fast signal/slots & utility library                      //
//                                                                                 //
//   v1.2 - public domain                                                          //
//   no warranty is offered or implied; use this code at your own risk             //
//                                                                                 //
// AUTHORS                                                                         //
//                                                                                 //
//   Written by Michael Bleis                                                      //
//                                                                                 //
//                                                                                 //
// LICENSE                                                                         //
//                                                                                 //
//   This software is dual-licensed to the public domain and under the following   //
//   license: you are granted a perpetual, irrevocable license to copy, modify,    //
//   publish, and distribute this file as you see fit.                             //
/////////////////////////////////////////////////////////////////////////////////////

// Note: This file has been created with contents of the signal.h file to make the
// stable_list class more lightweight to access (without including the whole signal.h
// file). The class has also been moved to the mmo namespace.

#pragma once

#include "intrusive_ptr.h"

#include <iterator>
#include <utility>
#include <initializer_list>


namespace mmo
{
	template <class T>
	class stable_list
	{
		struct link_element : ref_counted<link_element>
		{
			link_element() = default;

			~link_element()
			{
				if (next) {             // If we have a next element upon destruction
					value()->~T();      // then this link is used, else it's a dummy
				}
			}

			template <class... Args>
			void construct(Args&&... args)
			{
				new (storage()) T{ std::forward<Args>(args)... };
			}

			T* value()
			{
				return static_cast<T*>(storage());
			}

			void* storage()
			{
				return static_cast<void*>(&buffer);
			}

			intrusive_ptr<link_element> next;
			intrusive_ptr<link_element> prev;

			std::aligned_storage_t<sizeof(T), std::alignment_of<T>::value> buffer;
		};

		intrusive_ptr<link_element> head;
		intrusive_ptr<link_element> tail;

	public:
		template <class U>
		struct iterator_base 
			: std::iterator<std::bidirectional_iterator_tag, U>
		{
			typedef U value_type;
			typedef U& reference;
			typedef U* pointer;

			iterator_base() = default;
			~iterator_base() = default;

			iterator_base(iterator_base const& i)
				: element{ i.element }
			{
			}

			iterator_base(iterator_base&& i)
				: element{ std::move(i.element) }
			{
			}

			template <class V>
			explicit iterator_base(iterator_base<V> const& i)
				: element{ i.element }
			{
			}

			template <class V>
			explicit iterator_base(iterator_base<V>&& i)
				: element{ std::move(i.element) }
			{
			}

			iterator_base& operator = (iterator_base const& i)
			{
				element = i.element;
				return *this;
			}

			iterator_base& operator = (iterator_base&& i)
			{
				element = std::move(i.element);
				return *this;
			}

			template <class V>
			iterator_base& operator = (iterator_base<V> const& i)
			{
				element = i.element;
				return *this;
			}

			template <class V>
			iterator_base& operator = (iterator_base<V>&& i)
			{
				element = std::move(i.element);
				return *this;
			}

			iterator_base& operator ++ ()
			{
				element = element->next;
				return *this;
			}

			iterator_base operator ++ (int)
			{
				iterator_base i{ *this };
				++(*this);
				return i;
			}

			iterator_base& operator -- ()
			{
				element = element->prev;
				return *this;
			}

			iterator_base operator -- (int)
			{
				iterator_base i{ *this };
				--(*this);
				return i;
			}

			reference operator * () const
			{
				return *element->value();
			}

			pointer operator -> () const
			{
				return element->value();
			}

			template <class V>
			bool operator == (iterator_base<V> const& i) const
			{
				return element == i.element;
			}

			template <class V>
			bool operator != (iterator_base<V> const& i) const
			{
				return element != i.element;
			}

		private:
			template <class> friend class stable_list;

			intrusive_ptr<link_element> element;

			iterator_base(link_element* p)
				: element{ p }
			{
			}
		};

		typedef T value_type;
		typedef T& reference;
		typedef T* pointer;

		typedef iterator_base<T> iterator;
		typedef iterator_base<T const> const_iterator;
		typedef std::reverse_iterator<iterator> reverse_iterator;
		typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

		stable_list()
		{
			init();
		}

		~stable_list()
		{
			destroy();
		}

		stable_list(stable_list const& l)
		{
			init();
			insert(end(), l.begin(), l.end());
		}

		stable_list(stable_list&& l)
			: head{ std::move(l.head) }
			, tail{ std::move(l.tail) }
		{
			l.init();
		}

		stable_list(std::initializer_list<value_type> l)
		{
			init();
			insert(end(), l.begin(), l.end());
		}

		template <class Iterator>
		stable_list(Iterator ibegin, Iterator iend)
		{
			init();
			insert(end(), ibegin, iend);
		}

		stable_list& operator = (stable_list const& l)
		{
			if (this != &l) {
				clear();
				insert(end(), l.begin(), l.end());
			}
			return *this;
		}

		stable_list& operator = (stable_list&& l)
		{
			destroy();
			head = std::move(l.head);
			tail = std::move(l.tail);
			l.init();
			return *this;
		}

		iterator begin()
		{
			return iterator{ head->next };
		}

		iterator end()
		{
			return iterator{ tail };
		}

		const_iterator begin() const
		{
			return const_iterator{ head->next };
		}

		const_iterator end() const
		{
			return const_iterator{ tail };
		}

		const_iterator cbegin() const
		{
			return const_iterator{ head->next };
		}

		const_iterator cend() const
		{
			return const_iterator{ tail };
		}

		reverse_iterator rbegin()
		{
			return reverse_iterator{ end() };
		}

		reverse_iterator rend()
		{
			return reverse_iterator{ begin() };
		}

		const_reverse_iterator rbegin() const
		{
			return const_reverse_iterator{ cend() };
		}

		const_reverse_iterator rend() const
		{
			return const_reverse_iterator{ cbegin() };
		}

		const_reverse_iterator crbegin() const
		{
			return const_reverse_iterator{ cend() };
		}

		const_reverse_iterator crend() const
		{
			return const_reverse_iterator{ cbegin() };
		}

		reference front()
		{
			return *begin();
		}

		reference back()
		{
			return *rbegin();
		}

		value_type const& front() const
		{
			return *cbegin();
		}

		value_type const& back() const
		{
			return *crbegin();
		}

		bool empty() const
		{
			return cbegin() == cend();
		}

		void clear()
		{
			erase(begin(), end());
		}

		void push_front(value_type const& value)
		{
			insert(begin(), value);
		}

		void push_front(value_type&& value)
		{
			insert(begin(), std::move(value));
		}

		void push_back(value_type const& value)
		{
			insert(end(), value);
		}

		void push_back(value_type&& value)
		{
			insert(end(), std::move(value));
		}

		template <class... Args>
		void emplace_front(Args&&... args)
		{
			emplace(begin(), std::forward<Args>(args)...);
		}

		template <class... Args>
		void emplace_back(Args&&... args)
		{
			emplace(end(), std::forward<Args>(args)...);
		}

		void pop_front()
		{
			head->next = head->next->next;
			head->next->prev = head;
		}

		void pop_back()
		{
			tail->prev = tail->prev->prev;
			tail->prev->next = tail;
		}

		iterator insert(iterator const& pos, value_type const& value)
		{
			return iterator{ make_link(pos.element, value) };
		}

		iterator insert(iterator const& pos, value_type&& value)
		{
			return iterator{ make_link(pos.element, std::move(value)) };
		}

		template <class Iterator>
		void insert(iterator const& pos, Iterator ibegin, Iterator iend)
		{
			while (ibegin != iend) {
				insert(pos, *ibegin++);
			}
		}

		template <class... Args>
		iterator emplace(iterator const& pos, Args&&... args)
		{
			return iterator{ make_link(pos.element, std::forward<Args>(args)...) };
		}

		void append(std::initializer_list<value_type> l)
		{
			append(end(), l);
		}

		void append(iterator const& pos, std::initializer_list<value_type> l)
		{
			insert(pos, l.begin(), l.end());
		}

		iterator erase(iterator const& pos)
		{
			pos.element->prev->next = pos.element->next;
			pos.element->next->prev = pos.element->prev;
			return iterator{ pos.element->next };
		}

		iterator erase(iterator const& first, iterator const& last)
		{
			auto link = first.element;
			while (link != last.element) {
				auto next = link->next;
				link->prev = first.element->prev;
				link->next = last.element;
				link = std::move(next);
			}

			first.element->prev->next = last.element;
			last.element->prev = first.element->prev;
			return last;
		}

		void remove(value_type const& value)
		{
			for (auto itr = begin(); itr != end(); ++itr) {
				if (*itr == value) {
					erase(itr);
				}
			}
		}

		template <class Predicate>
		void remove_if(Predicate const& pred)
		{
			for (auto itr = begin(); itr != end(); ++itr) {
				if (pred(*itr)) {
					erase(itr);
				}
			}
		}

		void swap(stable_list& other)
		{
			if (this != &other) {
				auto t{ std::move(*this) };
				*this = std::move(other);
				other = std::move(t);
			}
		}

	private:
		void init()
		{
			head = new link_element;
			tail = new link_element;
			head->next = tail;
			tail->prev = head;
		}

		void destroy()
		{
			clear();
			head->next = nullptr;
			tail->prev = nullptr;
		}

		template <class... Args>
		link_element* make_link(link_element* l, Args&&... args)
		{
			intrusive_ptr<link_element> link{ new link_element };
			link->construct(std::forward<Args>(args)...);
			link->prev = l->prev;
			link->next = l;
			link->prev->next = link;
			link->next->prev = link;
			return link;
		}
	};
}
