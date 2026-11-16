#include "global_header.hpp"

#include "game/Think.hpp"

#include <cstdint>
#include <cstdlib>

#include <nds.h>
#include <nds/cothread.h>

#include "game/GameApi.hpp"

namespace hlrt
{
    Think think;

    namespace
    {
        hldata::ThinkQueue queue;
        hldata::LogicWorld *world = nullptr; // the map's, while there is one
        volatile bool busy = false;          // a question is being worked out

        /// The worker answers the questions while the frame waits for the
        /// screen. (Off, every question is answered as it is asked, inside
        /// the frame, as it was until 2026-11-16.) Not in the Download Play
        /// child: what it would add to its program is room a guest's map
        /// needs, and it only meets monsters in the demo.
#ifdef HLDS_CHILD
        constexpr bool kUseWorker = false;
#else
        constexpr bool kUseWorker = true;
#endif

        /// Under the frame: the scheduler runs the highest of the threads
        /// that are not waiting, so the worker only has the CPU while the
        /// frame waits for the screen (and the wireless threads, at the
        /// frame's own level, for what they have to do).
        constexpr int kWorkerPriority = -1;

#ifndef HLDS_CHILD
        // The worker's stack: its own, not the heap's. Measured 2 KB deep at
        // most (2026-11-16: c1a3a's soldiers, c3a1, c2a2a, DS and DSi); a
        // DS's maps are judged with what is left of the heap, so it is no
        // bigger than that and a margin. Its first words are a pattern the
        // guard looks at every frame, as the frame's own stack's are.
        constexpr std::size_t kStackBytes = 6 * 1024;
        alignas(8) std::uint32_t stack[kStackBytes / 4];
        constexpr std::uint32_t kGuardWords = 16;
        constexpr std::uint32_t kGuardMark = 0x5448494Eu; // "THIN"
#ifdef HLDS_PROFILE
        constexpr std::uint32_t kStackPaint = 0x7E1A5B3Du;
#endif
#endif

        // What detach() waits on while a question is being finished: the
        // worker sends it as it puts one down. (An address in main RAM: the
        // top bit, libnds's own, is clear.)
        std::uint32_t idleSignal() { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&busy)); }

#ifndef HLDS_CHILD
        cothread_t mainThread = -1;

        // Where long work stops a moment: the frame, if the screen has woken
        // it, has the CPU at once; if not, the worker goes straight on. Not on
        // the frame's own thread (a question answered as it is asked), and not
        // while a map is being left (then it is finished without stopping).
        void pause()
        {
            if (!queue.abort && cothread_get_current() != mainThread)
                cothread_yield();
        }

        // The worker: a question at a time while there are any; otherwise
        // asleep until the next frame's wait.
        int work(void *)
        {
            for (;;)
            {
                if (world != nullptr && !queue.abort)
                {
                    busy = true;
                    const bool did = gameApi.thinkRunOne(*world, pause);
                    busy = false;
                    cothread_send_signal(idleSignal());
                    if (did)
                    {
                        ++think.answered;
                        cothread_yield();
                        continue;
                    }
                }
                cothread_yield_irq(IRQ_VBLANK);
            }
            return 0;
        }
#endif
    } // namespace

    void Think::start()
    {
        if (started_ || !kUseWorker)
            return;
#ifndef HLDS_CHILD
#ifdef HLDS_PROFILE
        for (std::uint32_t &w : stack)
            w = kStackPaint;
#endif
        for (std::uint32_t i = 0; i < kGuardWords; ++i)
            stack[i] = kGuardMark ^ i;
        mainThread = cothread_get_current();
        const cothread_t thread = cothread_create_manual(work, nullptr, stack, kStackBytes, 0);
        if (thread < 0)
            return;
        cothread_set_priority(thread, kWorkerPriority);
        started_ = true;
#endif
    }

    std::uint32_t Think::stackPeak() const
    {
#if defined(HLDS_PROFILE) && !defined(HLDS_CHILD)
        // (The stack grows down: what was never written is at the bottom.)
        std::size_t untouched = kGuardWords;
        while (untouched < kStackBytes / 4 && stack[untouched] == kStackPaint)
            ++untouched;
        return static_cast<std::uint32_t>(kStackBytes - untouched * 4);
#else
        return 0;
#endif
    }

    const void *Think::stackOverflowed() const
    {
#ifndef HLDS_CHILD
        if (!started_)
            return nullptr;
        for (std::uint32_t i = 0; i < kGuardWords; ++i)
        {
            if (stack[i] != (kGuardMark ^ i))
                return stack;
        }
#endif
        return nullptr;
    }

    void Think::attach(hldata::LogicWorld &w)
    {
        detach();
        if (w.actors == nullptr || w.actors->count <= 0)
            return;
        queue.slots = static_cast<hldata::ThinkSlot *>(
            std::calloc(static_cast<std::size_t>(w.actors->count), sizeof(hldata::ThinkSlot)));
        if (queue.slots == nullptr)
            return;
        queue.count = w.actors->count;
        gameApi.thinkClear(queue);
        queue.deferred = started_;
        // (A DSi's monsters look further for their ways: the worker has the
        // time for it.)
        queue.farPlans = started_ && isDSiMode();
        queue.abort = false;
        w.actors->think = &queue;
        world = &w;
    }

    void Think::detach()
    {
        // (Whatever is half worked out is finished now -- it no longer stops
        // -- before what it reads goes. The frame waits for it asleep: the
        // worker, under it, would never have the CPU if the frame only
        // yielded.)
        queue.abort = true;
        while (busy)
            cothread_yield_signal(idleSignal());
        if (world != nullptr && world->actors != nullptr)
            world->actors->think = nullptr;
        world = nullptr;
        std::free(queue.slots);
        queue.slots = nullptr;
        queue.count = 0;
        queue.deferred = false;
        queue.farPlans = false;
        queue.abort = false;
    }
} // namespace hlrt
