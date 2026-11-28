#include "global_header.hpp"

#include "game/Think.hpp"

#include <cstdint>
#include <cstdlib>

#include <nds.h>
#include <nds/cothread.h>

#include "DebugLog.hpp"
#include "game/GameApi.hpp"
#include "HlTraceCount.hpp"
#include "net/NetLink.hpp"

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
        // The world, held: by the frame's thread all the time but while it
        // waits for the screen (waitForScreen), by the worker while it works
        // a question out but at its pauses. libnds cuts a thread off at any
        // interrupt; under this, the world still only changes between the
        // worker's pauses -- the frame, woken in the middle of a question,
        // waits for the next one (a few milliseconds at most).
        comutex_t thinking = 0;
        // The worker in the middle of a question (set and cleared holding
        // the world), and told when it is out of one: detach() waits for it.
        volatile bool busy = false;
        cosema_t questionOver = 0;
        // (comutex and cosema hold the interrupts off a moment, by CPSR.I,
        // and put them back on. Neither is ever used with the interrupts held
        // off, nor from an interrupt: there, cothread_send_signal only.)

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

#ifndef HLDS_CHILD
        cothread_t mainThread = -1;

#ifdef HLDS_PROFILE
        // How long the frame waits for the world, woken by the screen, and
        // the worker's stretches between its pauses (Think::takeHoldups).
        Think::Holdups held;
        constexpr std::uint32_t kMillisecond = 33514; // (bus ticks: DebugLog's clock)
        std::uint32_t stretchFrom = 0, stretchRadio = 0;
        int stretchKind = -1; // the question the worker is in, once known

        // The question being worked out, if there is one: its slot's kind
        // and its actor. (The worker under the frame: while this runs, it
        // stands where it was cut off, and its slot with it.)
        int workingSlot()
        {
            if (queue.slots == nullptr)
                return -1;
            // (thinkRunOne sets `next` past the one it took before it starts.)
            const int guess = queue.next - 1;
            if (guess >= 0 && guess < queue.count && queue.slots[guess].state == hldata::kThinkWorking)
                return guess;
            for (int i = 0; i < queue.count; ++i)
            {
                if (queue.slots[i].state == hldata::kThinkWorking)
                    return i;
            }
            return -1;
        }

        void noteHoldup(Think::Holdup &h, std::uint32_t ticks, std::uint32_t radio, int slot)
        {
            if (ticks > kMillisecond)
                ++h.over;
            if (ticks <= h.worst)
                return;
            h.worst = ticks;
            h.radio = radio;
            h.actor = static_cast<std::int16_t>(slot);
            const std::uint8_t kind = slot >= 0 ? queue.slots[slot].kind : 0;
            h.kind = kind < hldata::kThinkKinds ? kind : 0;
        }

        // A stretch of the worker's, from its start (or a pause) to its end
        // (or the next pause).
        void stretchEnd()
        {
            if (stretchKind < 0)
            {
                const int slot = workingSlot();
                const std::uint8_t kind = slot >= 0 ? queue.slots[slot].kind : 0;
                stretchKind = kind < hldata::kThinkKinds ? kind : 0;
            }
            const std::uint32_t radio = DebugLog::Perf::radioTicks() - stretchRadio;
            const std::uint32_t wall = DebugLog::Perf::now() - stretchFrom;
            const std::uint32_t ticks = wall > radio ? wall - radio : 0;
            if (ticks > held.stretchOf[stretchKind])
                held.stretchOf[stretchKind] = ticks;
            if (ticks > held.mapStretchOf[stretchKind])
                held.mapStretchOf[stretchKind] = ticks;
        }
#endif

        // Where long work stops a moment, the world let go: the frame, if
        // the screen has woken it, has it then -- and the CPU, being above
        // -- and gives it back as it waits for the screen again; if not, the
        // worker goes straight on. Not on the frame's own thread (a question
        // answered as it is asked), and not while a map is being left (then
        // it is finished without stopping).
        void pause()
        {
            if (!queue.abort && cothread_get_current() != mainThread)
            {
#if defined(HLDS_PROFILE)
                stretchEnd();
                // (Its time away, not the trace's it stopped in: HlTraceCount.hpp.)
                const std::uint32_t left = hlProfileNow();
#endif
                comutex_release(&thinking);
                cothread_yield();
                comutex_acquire(&thinking);
#if defined(HLDS_PROFILE)
                hlProfileAway += hlProfileNow() - left;
                stretchFrom = DebugLog::Perf::now();
                stretchRadio = DebugLog::Perf::radioTicks();
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
                    busy = true;
#ifdef HLDS_PROFILE
                    stretchFrom = DebugLog::Perf::now();
                    stretchRadio = DebugLog::Perf::radioTicks();
                    stretchKind = -1;
#endif
                    const bool did = world != nullptr && !queue.abort && gameApi.thinkRunOne(*world, pause);
#ifdef HLDS_PROFILE
                    // (Its last stretch; none if there was no question.)
                    if (did)
                    {
                        if (stretchKind < 0 && queue.slots != nullptr && queue.next > 0 &&
                            queue.next <= queue.count)
                        {
                            const std::uint8_t kind = queue.slots[queue.next - 1].kind;
                            stretchKind = kind < hldata::kThinkKinds ? kind : 0;
                        }
                        stretchEnd();
                    }
#endif
                    busy = false;
                    comutex_release(&thinking);
                    cosema_signal(&questionOver);
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
        cosema_init(&questionOver, 0);
        comutex_init(&thinking);
        // (The world is the frame's from here on, but while it waits for the
        // screen. The program's statics are made without a lock as well --
        // source/StdHalts.cpp -- and are first made by whichever holds it.)
        comutex_acquire(&thinking);
        frameHolds_ = true;
        const cothread_t thread =
            cothread_create_priority_manual(work, nullptr, stack, kStackBytes, kWorkerPriority, 0);
        if (thread < 0)
        {
            comutex_release(&thinking);
            frameHolds_ = false;
            return;
        }
        started_ = true;
#endif
    }

    void Think::waitForScreen(void (*wait)())
    {
#ifndef HLDS_CHILD
        if (!started_)
        {
            wait();
            return;
        }
        frameHolds_ = false;
        comutex_release(&thinking);
        wait();
#ifdef HLDS_PROFILE
        // (How long the world takes to come back, and whose question kept
        // it: the worker, cut off where it was, still in it now.)
        const std::uint32_t woke = DebugLog::Perf::now();
        const std::uint32_t radioWas = DebugLog::Perf::radioTicks();
        const int slot = workingSlot();
#endif
        // (Woken in the middle of a question: back at its next pause.)
        comutex_acquire(&thinking);
        frameHolds_ = true;
#ifdef HLDS_PROFILE
        const std::uint32_t ticks = DebugLog::Perf::now() - woke;
        const std::uint32_t radio = DebugLog::Perf::radioTicks() - radioWas;
        noteHoldup(held.window, ticks, radio, slot);
        noteHoldup(held.map, ticks, radio, slot);
        if (netlink::radioOn())
            noteHoldup(held.match, ticks, radio, slot);
#endif
#else
        wait();
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
#if defined(HLDS_PROFILE) && !defined(HLDS_CHILD)
        // (A map's worst is its own.)
        held.map = Holdup();
        held.match = Holdup();
        for (std::uint32_t &t : held.mapStretchOf)
            t = 0;
#endif
    }

    Think::Holdups Think::takeHoldups()
    {
#if defined(HLDS_PROFILE) && !defined(HLDS_CHILD)
        const Holdups h = held;
        held.window = Holdup();
        for (std::uint32_t &t : held.stretchOf)
            t = 0;
        return h;
#else
        return Holdups();
#endif
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
        for (int k = 0; k < hldata::kThinkKinds; ++k)
        {
            w.takenOf[k] = queue.takenOf[k];
            w.framesOf[k] = queue.framesOf[k];
            w.longestOf[k] = queue.longestOf[k];
            queue.takenOf[k] = queue.framesOf[k] = queue.longestOf[k] = 0;
        }
        // (What is still asked or worked out, or answered and not yet taken
        // up: how long it has been waiting.)
        if (queue.slots != nullptr)
        {
            for (int i = 0; i < queue.count; ++i)
            {
                const hldata::ThinkSlot &slot = queue.slots[i];
                if (slot.state == hldata::kThinkFree || slot.kind >= hldata::kThinkKinds)
                    continue;
                const std::uint32_t age = queue.frame - slot.askedFrame;
                if (age > w.waitingOf[slot.kind])
                    w.waitingOf[slot.kind] = age;
            }
        }
        return w;
    }

    void Think::detach()
    {
        // (Whatever is half worked out is finished now -- it no longer stops
        // -- before what it reads goes: the world let go, and the frame asleep
        // until the worker is out of its question. Asleep: the worker, under
        // it, would never have the CPU if the frame only yielded.)
        queue.abort = true;
#ifndef HLDS_CHILD
        if (started_ && busy)
        {
            while (cosema_try_wait(&questionOver))
            {
            }
            comutex_release(&thinking);
            while (busy)
                cosema_wait(&questionOver);
            comutex_acquire(&thinking);
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
