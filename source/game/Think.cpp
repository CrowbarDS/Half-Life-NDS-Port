#include "global_header.hpp"

#include "game/Think.hpp"

#include <cstdint>
#include <cstdlib>

#include <nds.h>
#include <nds/cothread.h>

#include "game/GameApi.hpp"
#include "HlTraceCount.hpp"

namespace hlrt
{
    Think think;

    namespace
    {
        hldata::ThinkQueue queue;
        hldata::LogicWorld *world = nullptr; // the map's, while there is one
        // Counted up for each question asked (ThinkQueue::posted): the worker
        // sleeps on it, rather than waking every frame to find nothing.
        cosema_t asked = 0;
        // Held by the worker while it works a question out: what detach()
        // waits for before what the question reads goes.
        comutex_t thinking = 0;
        // (comutex and cosema hold the interrupts off a moment and put them
        // back as they were -- on the pinned libnds; a later one turns them
        // on regardless. Neither is ever used with the interrupts held off,
        // nor from an interrupt: there, cothread_send_signal only.)

        /// The worker answers the questions while the frame waits for the
        /// screen. (Off, every question is answered as it is asked, inside
        /// the frame, as it was until 2026-11-16.) Not in the Download Play
        /// child: what it would add to its program is room a guest's map
        /// needs, and it only meets monsters in the demo.
        /// (Switched on or off, it only ever takes turns: see start().)
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

#ifndef HLDS_CHILD
        cothread_t mainThread = -1;

        // Where long work stops a moment: the frame, if the screen has woken
        // it, has the CPU at once; if not, the worker goes straight on. Not on
        // the frame's own thread (a question answered as it is asked), and not
        // while a map is being left (then it is finished without stopping).
        void pause()
        {
            if (!queue.abort && cothread_get_current() != mainThread)
            {
#if defined(HLDS_PROFILE)
                // (Its time away, not the trace's it stopped in: HlTraceCount.hpp.)
                const std::uint32_t left = hlProfileNow();
                cothread_yield();
                hlProfileAway += hlProfileNow() - left;
#else
                cothread_yield();
#endif
            }
        }

        // A question asked: the worker woken (from the frame's own thread).
        void posted() { cosema_signal(&asked); }

        // The worker: asleep until a question is asked, then a question at a
        // time while there are any. (No wake-up is missed: it only stops at
        // its own yields, and there is none between finding the questions
        // all answered and going back to sleep.)
        int work(void *)
        {
            for (;;)
            {
                cosema_wait(&asked);
                // (Several asked since it last looked are all answered now.)
                while (cosema_try_wait(&asked))
                {
                }
                bool any = false;
                while (world != nullptr && !queue.abort)
                {
                    comutex_acquire(&thinking);
                    const bool did = world != nullptr && !queue.abort && gameApi.thinkRunOne(*world, pause);
                    comutex_release(&thinking);
                    if (!did)
                        break;
                    any = true;
                    ++think.answered;
                    cothread_yield();
                }
                if (!any)
                    ++think.idleWakes;
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
        cosema_init(&asked, 0);
        comutex_init(&thinking);
        // (It takes turns with the main thread, at its cothread_yield calls:
        // never cut off by it, nor it by this. The program's statics count
        // on that -- they are made without a lock, source/StdHalts.cpp -- and
        // the guard stops the game if libnds's preemption is ever switched
        // on, Guard.arm.cpp.)
        const cothread_t thread = cothread_create_manual(work, nullptr, stack, kStackBytes, 0);
        if (thread < 0)
            return;
        cothread_set_priority(thread, kWorkerPriority);
        started_ = true;
#endif
    }

    bool Think::onWorker() const
    {
#ifndef HLDS_CHILD
        return started_ && cothread_get_current() != mainThread;
#else
        return false;
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
        // (A DSi's monsters take an answer up the moment it is in: the
        // worker has the time to give them sooner. No further than the
        // game's do they look for it.)
        queue.answersAtOnce = started_ && isDSiMode();
        queue.abort = false;
        // (Each question asked wakes the worker: it sleeps otherwise.)
#ifndef HLDS_CHILD
        queue.posted = started_ ? &posted : nullptr;
#endif
        w.actors->think = &queue;
        world = &w;
    }

    Think::Waits Think::takeWaits()
    {
        Waits w;
        w.taken = queue.taken;
        w.frames = queue.takenFrames;
        w.longest = queue.longest;
        w.dropped = queue.dropped;
        w.gaveUp = queue.gaveUp;
        queue.taken = queue.takenFrames = queue.longest = queue.dropped = queue.gaveUp = 0;
        return w;
    }

    void Think::detach()
    {
        // (Whatever is half worked out is finished now -- it no longer stops
        // -- before what it reads goes. The frame waits for it asleep: the
        // worker, under it, would never have the CPU if the frame only
        // yielded.)
        queue.abort = true;
#ifndef HLDS_CHILD
        if (started_)
        {
            comutex_acquire(&thinking);
            comutex_release(&thinking);
        }
#endif
        if (world != nullptr && world->actors != nullptr)
            world->actors->think = nullptr;
        world = nullptr;
        std::free(queue.slots);
        queue.slots = nullptr;
        queue.count = 0;
        queue.deferred = false;
        queue.answersAtOnce = false;
        queue.posted = nullptr;
        queue.abort = false;
    }
} // namespace hlrt
