// The C++ library's ways out of trouble, without its exceptions.
//
// The program is built without exceptions, but the library's own ways of
// failing -- a vector asked for more than it can hold, an allocation that
// cannot be had, a function's static made the first time on two threads --
// are written to throw, and bring in with them the whole of the library's
// exception handling: about 11 KB of every console's heap, and 1 KB more it
// takes at start-up, for a throw that only ever ended the game anyway. Here
// each stops the game on the crash screen instead (Guard.hpp), saying what
// went wrong; an allocation that fails still asks the new-handler first,
// which says "Not enough memory" over the map's name (main.cpp).
//
// The functions' statics are made without the library's guard as well
// (-fno-threadsafe-statics, build.py): the worker thread (game/Think.cpp)
// only takes turns with the main one, so never runs while another is in the
// middle of one. Guard.arm.cpp stops the game if libnds's preemption, which
// would end that, is ever switched on.
//
// What is defined here must be all the library's own file has of what is
// used: one left out brings that file in again, and its definitions clash
// with these at the link.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#include "Guard.hpp"

void *operator new(std::size_t size)
{
    if (size == 0)
        size = 1;
    while (true)
    {
        if (void *p = std::malloc(size))
            return p;
        const std::new_handler handler = std::get_new_handler();
        if (handler == nullptr)
            hlrt::guard::crash("out of memory", static_cast<std::uint32_t>(size));
        handler();
    }
}

void *operator new[](std::size_t size)
{
    return operator new(size);
}

namespace std
{
    // (`what` names the container and what was asked of it:
    // "vector::reserve".)
    [[noreturn]] void __throw_length_error(const char *what)
    {
        hlrt::guard::crash(what != nullptr ? what : "length error");
    }

    [[noreturn]] void __throw_logic_error(const char *what)
    {
        hlrt::guard::crash(what != nullptr ? what : "logic error");
    }

    // (A format, not a message: said as it is.)
    [[noreturn]] void __throw_out_of_range_fmt(const char *what, ...)
    {
        hlrt::guard::crash(what != nullptr ? what : "out of range");
    }

    [[noreturn]] void __throw_bad_alloc()
    {
        hlrt::guard::crash("out of memory");
    }
} // namespace std

// The library's strings (made with exceptions) name what an exception passing
// through them would use: never run here, where a throw stops the game first.
// Each stops it too, should anything ever reach one.
extern "C"
{
    [[noreturn]] void __cxa_begin_catch()
    {
        hlrt::guard::crash("__cxa_begin_catch");
    }
    [[noreturn]] void __cxa_end_catch()
    {
        hlrt::guard::crash("__cxa_end_catch");
    }
    [[noreturn]] void __cxa_end_cleanup()
    {
        hlrt::guard::crash("__cxa_end_cleanup");
    }
    [[noreturn]] void __cxa_rethrow()
    {
        hlrt::guard::crash("__cxa_rethrow");
    }
    [[noreturn]] void __gxx_personality_v0()
    {
        hlrt::guard::crash("__gxx_personality_v0");
    }
    [[noreturn]] void __aeabi_unwind_cpp_pr0()
    {
        hlrt::guard::crash("__aeabi_unwind_cpp_pr0");
    }
    [[noreturn]] void __aeabi_unwind_cpp_pr1()
    {
        hlrt::guard::crash("__aeabi_unwind_cpp_pr1");
    }
    [[noreturn]] void __aeabi_unwind_cpp_pr2()
    {
        hlrt::guard::crash("__aeabi_unwind_cpp_pr2");
    }
    // (typeinfo for __cxxabiv1::__forced_unwind: only its address is named,
    // in the strings' tables of what they would catch.)
    extern const std::uint32_t _ZTIN10__cxxabiv115__forced_unwindE[2];
    const std::uint32_t _ZTIN10__cxxabiv115__forced_unwindE[2] = {0, 0};
}
