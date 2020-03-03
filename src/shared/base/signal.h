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

// Note: This file got split into the following files in the base library:
//  error.h:			mmo::error class
//  optional.h:			mmo::optional<T> class
//  intrusive_ptr.h:	mmo::intrusive_ptr<T,RefCount> class
//  stable_list.h:		mmo::stable_list<T> class
// Also, all classes defined have been moved to the mmo namespace. All occurrences
// of assert have also been replaced by the custom ASSERT macro of this project, 
// which is defined in macros.h of the base library.

#pragma once

#include "macros.h"
#include "optional.h"
#include "error.h"
#include "stable_list.h"

#include <memory>
#include <functional>
#include <list>
#include <forward_list>

/// Redefine this if your compiler doesn't support the thread_local keyword
/// For VS < 2015 you can define it to __declspec(thread) for example.
#ifndef SIMPLE_THREAD_LOCAL
#define SIMPLE_THREAD_LOCAL thread_local
#endif

// Note: This will be defined by cmake using a cmake option. Don't define
// in this code file since it has been split up.
/// Define this if you want to disable exceptions.
//#define SIMPLE_NO_EXCEPTIONS

namespace mmo
{
    template <class T>
    struct minimum
    {
        typedef T value_type;
        typedef T result_type;

        template <class U>
        void operator () (U&& value)
        {
            if (!has_value || value < current) {
                current = std::forward<U>(value);
                has_value = true;
            }
        }

        result_type result()
        {
            return std::move(current);
        }

    private:
        value_type current{};
        bool has_value{ false };
    };

    template <class T>
    struct maximum
    {
        typedef T value_type;
        typedef T result_type;

        template <class U>
        void operator () (U&& value)
        {
            if (!has_value || value > current) {
                current = std::forward<U>(value);
                has_value = true;
            }
        }

        result_type result()
        {
            return std::move(current);
        }

    private:
        value_type current{};
        bool has_value{ false };
    };

    template <class T>
    struct first
    {
        typedef T value_type;
        typedef T result_type;

        template <class U>
        void operator () (U&& value)
        {
            if (!has_value) {
                current = std::forward<U>(value);
                has_value = true;
            }
        }

        result_type result()
        {
            return std::move(current);
        }

    private:
        value_type current{};
        bool has_value{ false };
    };

    template <class T>
    struct last
    {
        typedef T value_type;
        typedef T result_type;

        template <class U>
        void operator () (U&& value)
        {
            current = std::forward<U>(value);
        }

        result_type result()
        {
            return std::move(current);
        }

    private:
        value_type current{};
    };

    template <class T>
    struct range
    {
        typedef T value_type;
        typedef std::list<T> result_type;

        template <class U>
        void operator () (U&& value)
        {
            values.emplace_back(std::forward<U>(value));
        }

        result_type result()
        {
            return std::move(values);
        }

    private:
        std::list<value_type> values;
    };

#ifndef SIMPLE_NO_EXCEPTIONS
    struct invocation_slot_error : error
    {
        const char* what() const throw() override
        {
            return "simplesig: One of the slots has raised an exception during the signal invocation.";
        }
    };
#endif

    namespace detail
    {
        template <class>
        struct expand_signature;

        template <class R, class... Args>
        struct expand_signature<R(Args...)>
        {
            typedef R result_type;
            typedef R signature_type(Args...);
        };

        struct connection_base : ref_counted<connection_base>
        {
            virtual ~connection_base() = default;

            virtual bool connected() const = 0;
            virtual void disconnect() = 0;
        };

        template <class T>
        struct functional_connection : connection_base
        {
            bool connected() const override
            {
                return slot != nullptr;
            }

            void disconnect() override
            {
                if (slot != nullptr) {
                    slot = nullptr;

                    next->prev = prev;
                    prev->next = next;
                }
            }

            std::function<T> slot;

            intrusive_ptr<functional_connection> next;
            intrusive_ptr<functional_connection> prev;
        };

        // Should make sure that this is POD
        struct thread_local_data
        {
            connection_base* current_connection;
            bool emission_aborted;
        };

        inline thread_local_data* get_thread_local_data()
        {
            static SIMPLE_THREAD_LOCAL thread_local_data th;
            return &th;
        }

        struct connection_scope
        {
            connection_scope(connection_base* base, thread_local_data* th)
                : th{ th }
                , prev{ th->current_connection }
            {
                th->current_connection = base;
            }

            ~connection_scope()
            {
                th->current_connection = prev;
            }

            thread_local_data* th;
            connection_base* prev;
        };

        struct abort_scope
        {
            abort_scope(thread_local_data* th)
                : th{ th }
                , prev{ th->emission_aborted }
            {
                th->emission_aborted = false;
            }

            ~abort_scope()
            {
                th->emission_aborted = prev;
            }

            thread_local_data* th;
            bool prev;
        };
    }

    struct connection
    {
        connection() = default;
        ~connection() = default;

        connection(connection&& rhs) noexcept
            : base{ std::move(rhs.base) }
        {
        }

        connection(connection const& rhs) noexcept
            : base{ rhs.base }
        {
        }

        explicit connection(detail::connection_base* base) noexcept
            : base{ base }
        {
        }

        connection& operator = (connection&& rhs)
        {
            base = std::move(rhs.base);
            return *this;
        }

        connection& operator = (connection const& rhs)
        {
            base = rhs.base;
            return *this;
        }

        bool operator == (connection const& rhs) const
        {
            return base == rhs.base;
        }

        bool operator != (connection const& rhs) const
        {
            return base != rhs.base;
        }

        explicit operator bool() const
        {
            return connected();
        }

        bool connected() const
        {
            return base ? base->connected() : false;
        }

        void disconnect()
        {
            if (base != nullptr) {
                base->disconnect();
                base = nullptr;
            }
        }

        void swap(connection& other)
        {
            if (this != &other) {
                auto t{ std::move(*this) };
                *this = std::move(other);
                other = std::move(t);
            }
        }

    private:
        intrusive_ptr<detail::connection_base> base;
    };

    struct scoped_connection : connection
    {
        scoped_connection() = default;

        ~scoped_connection()
        {
            disconnect();
        }

        explicit scoped_connection(connection const& rhs)
            : connection{ rhs }
        {
        }

        explicit scoped_connection(connection&& rhs)
            : connection{ std::move(rhs) }
        {
        }

        scoped_connection(scoped_connection&& rhs)
            : connection{ std::move(rhs) }
        {
        }

        scoped_connection& operator = (connection&& rhs)
        {
            disconnect();

            connection::operator=(std::move(rhs));
            return *this;
        }

        scoped_connection& operator = (scoped_connection&& rhs)
        {
            disconnect();

            connection::operator=(std::move(rhs));
            return *this;
        }

        scoped_connection& operator = (connection const& rhs)
        {
            disconnect();

            connection::operator=(rhs);
            return *this;
        }

    private:
        scoped_connection(scoped_connection const&) = delete;

        scoped_connection& operator = (scoped_connection const&) = delete;
    };

    struct scoped_connection_container
    {
        scoped_connection_container() = default;
        ~scoped_connection_container() = default;

        scoped_connection_container(scoped_connection_container&& s)
            : connections{ std::move(s.connections) }
        {
        }

        scoped_connection_container& operator = (scoped_connection_container&& rhs)
        {
            connections = std::move(rhs.connections);
            return *this;
        }

        scoped_connection_container(std::initializer_list<connection> list)
        {
            append(list);
        }

        void append(connection const& conn)
        {
            connections.push_front(scoped_connection{ conn });
        }

        void append(std::initializer_list<connection> list)
        {
            for (auto const& connection : list) {
                append(connection);
            }
        }

        scoped_connection_container& operator += (connection const& conn)
        {
            append(conn);
            return *this;
        }

        void disconnect()
        {
            connections.clear();
        }

    private:
        scoped_connection_container(scoped_connection_container const&) = delete;
        scoped_connection_container& operator = (scoped_connection_container const&) = delete;

        std::forward_list<scoped_connection> connections;
    };

    struct trackable
    {
        void add_tracked_connection(connection const& conn)
        {
            container.append(conn);
        }

        void disconnect_tracked_connections()
        {
            container.disconnect();
        }

    private:
        scoped_connection_container container;
    };

    inline connection current_connection()
    {
        return connection{ detail::get_thread_local_data()->current_connection };
    }

    inline void abort_emission()
    {
        detail::get_thread_local_data()->emission_aborted = true;
    }

    template <class T>
    struct default_collector : last<optional<T>>
    {
    };

    template <>
    struct default_collector<void>
    {
        typedef void value_type;
        typedef void result_type;

        void operator () ()
        {
            /* do nothing for void types */
        }

        void result()
        {
            /* do nothing for void types */
        }
    };

    template <class Signature, class Collector = default_collector<
        typename detail::expand_signature<Signature>::result_type>>
    struct signal;

    template <class Collector, class R, class... Args>
    struct signal<R(Args...), Collector>
    {
        typedef R signature_type(Args...);
        typedef std::function<signature_type> slot_type;

        signal()
        {
            init();
        }

        ~signal()
        {
            destroy();
        }

        signal(signal&& s)
            : head{ std::move(s.head) }
            , tail{ std::move(s.tail) }
        {
            s.init();
        }

        signal(signal const& s)
        {
            init();
            copy(s);
        }

        signal& operator = (signal&& rhs)
        {
            destroy();
            head = std::move(rhs.head);
            tail = std::move(rhs.tail);
            rhs.init();
            return *this;
        }

        signal& operator = (signal const& rhs)
        {
            if (this != &rhs) {
                clear();
                copy(rhs);
            }
            return *this;
        }

        connection connect(slot_type slot, bool first = false)
        {
            ASSERT(slot != nullptr);

            detail::connection_base* base = make_link(
                first ? head->next : tail, std::move(slot));
            return connection{ base };
        }

        template <class R1, class... Args1>
        connection connect(R1(*method)(Args1...), bool first = false)
        {
            return connect([method](Args... args) {
                return R((*method)(Args1(args)...));
            }, first);
        }

        template <class Instance, class Class, class R1, class... Args1>
        connection connect(Instance& object, R1(Class::*method)(Args1...), bool first = false)
        {
            connection c{
                connect([&object, method](Args... args) {
                    return R((object.*method)(Args1(args)...));
                }, first)
            };
            maybe_add_tracked_connection(&object, c);
            return c;
        }

        template <class Instance, class Class, class R1, class... Args1>
        connection connect(Instance* object, R1(Class::*method)(Args1...), bool first = false)
        {
            connection c{
                connect([object, method](Args... args) {
                    return R((object->*method)(Args1(args)...));
                }, first)
            };
            maybe_add_tracked_connection(object, c);
            return c;
        }

        connection operator += (slot_type slot)
        {
            return connect(std::move(slot));
        }

        void clear()
        {
            intrusive_ptr<connection_base> current{ head->next };
            while (current != tail) {
                intrusive_ptr<connection_base> next{ current->next };
                current->slot = nullptr;
                current->next = tail;
                current->prev = head;
                current = std::move(next);
            }

            head->next = tail;
            tail->prev = head;
        }

        void swap(signal& other)
        {
            if (this != &other) {
                auto t{ std::move(*this) };
                *this = std::move(other);
                other = std::move(t);
            }
        }

        template <class ValueCollector = Collector>
        auto invoke(Args const&... args) const -> decltype(ValueCollector{}.result())
        {
#ifndef SIMPLE_NO_EXCEPTIONS
            bool error{ false };
#endif
            ValueCollector collector{};
            {
                detail::thread_local_data* th{ detail::get_thread_local_data() };
                detail::abort_scope ascope{ th };

                intrusive_ptr<connection_base> current{ head->next };
                intrusive_ptr<connection_base> end{ tail };

                while (current != end) {
                    ASSERT(current != nullptr);

                    if (current->slot != nullptr) {
                        detail::connection_scope cscope{ current, th };
#ifndef SIMPLE_NO_EXCEPTIONS
                        try {
#endif
                            invoke(collector, current->slot, args...);
#ifndef SIMPLE_NO_EXCEPTIONS
                        } catch (...) {
                            error = true;
                        }
#endif
                        if (th->emission_aborted) {
                            break;
                        }
                    }

                    current = current->next;
                }
            }

#ifndef SIMPLE_NO_EXCEPTIONS
            if (error) {
                throw invocation_slot_error{};
            }
#endif
            return collector.result();
        }

        auto operator () (Args const&... args) const -> decltype(invoke(args...))
        {
            return invoke(args...);
        }

    private:
        typedef detail::functional_connection<signature_type> connection_base;

        template <class ValueCollector, class T = R>
        std::enable_if_t<std::is_void<T>::value, void>
            invoke(ValueCollector& collector, slot_type const& slot, Args const&... args) const
        {
            slot(args...); collector();
        }

        template <class ValueCollector, class T = R>
        std::enable_if_t<!std::is_void<T>::value, void>
            invoke(ValueCollector& collector, slot_type const& slot, Args const&... args) const
        {
            collector(slot(args...));
        }

        void init()
        {
            head = new connection_base;
            tail = new connection_base;
            head->next = tail;
            tail->prev = head;
        }

        void destroy()
        {
            clear();
            head->next = nullptr;
            tail->prev = nullptr;
        }

        void copy(signal const& s)
        {
            intrusive_ptr<connection_base> current{ s.head->next };
            intrusive_ptr<connection_base> end{ s.tail };

            while (current != end) {
                make_link(tail, current->slot);
                current = current->next;
            }
        }

        connection_base* make_link(connection_base* l, slot_type slot)
        {
            intrusive_ptr<connection_base> link{ new connection_base };
            link->slot = std::move(slot);
            link->prev = l->prev;
            link->next = l;
            link->prev->next = link;
            link->next->prev = link;
            return link;
        }

        template <class Instance>
        std::enable_if_t<!std::is_base_of<trackable, Instance>::value, void>
            maybe_add_tracked_connection(Instance*, connection)
        {
        }

        template <class Instance>
        std::enable_if_t<std::is_base_of<trackable, Instance>::value, void>
            maybe_add_tracked_connection(Instance* inst, connection conn)
        {
            static_cast<trackable*>(inst)->add_tracked_connection(conn);
        }

        intrusive_ptr<connection_base> head;
        intrusive_ptr<connection_base> tail;
    };

    template <class Instance, class Class, class R, class... Args>
    std::function<R(Args...)> slot(Instance& object, R(Class::*method)(Args...))
    {
        return [&object, method](Args... args) {
            return (object.*method)(args...);
        };
    }

    template <class Instance, class Class, class R, class... Args>
    std::function<R(Args...)> slot(Instance* object, R(Class::*method)(Args...))
    {
        return [object, method](Args... args) {
            return (object->*method)(args...);
        };
    }
}
