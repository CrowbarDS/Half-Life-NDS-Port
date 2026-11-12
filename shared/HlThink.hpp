#pragma once

// The actors' long thinking, done outside the frame (HlActors.hpp,
// ThinkQueue): a question an actor has asked -- its way somewhere -- worked
// out by the same code that would have worked it
// out in its tick, and its answer left for the actor to take up.
//
// On the console a worker of the 3D engine's runs thinkRunOne while the
// frame waits for the screen (source/game/Think.cpp), stopping now and then
// (thinkYield) so as never to hold the frame up for long. Here it is only
// the work, std-only: the host's tests run it between ticks.

#include "HlMonsters.hpp"

namespace hldata
{
    /// Works out one question that is waiting, if there is one. False if
    /// there was none.
    inline bool thinkRunOne(LogicWorld &w)
    {
        if (w.actors == nullptr || w.actors->think == nullptr || w.world == nullptr)
            return false;
        ActorWorld &a = *w.actors;
        ThinkQueue &q = *a.think;
        const int count = q.count < a.count ? q.count : a.count;
        int index = -1;
        for (int n = 0; n < count && index < 0; ++n)
        {
            const int i = (q.next + n) % (count > 0 ? count : 1);
            if (q.slots[i].state == kThinkAsked)
                index = i;
        }
        if (index < 0)
            return false;
        q.next = index + 1;
        ThinkSlot &slot = q.slots[index];
        slot.state = kThinkWorking;
        // (What was asked, as it was asked: a copy, since the actor may ask
        // again while this is being worked out.)
        const ThinkSlot asked = slot;
        ThinkSlot answer = asked;
        switch (asked.kind)
        {
        case kThinkPlan:
        {
            ActorState walker = a.states[index];
            int door = -1;
            detail::walkPlanInto(a, *w.world, index, walker, asked.small, asked.at, asked.reach, door);
            answer.found = walker.hasGoal;
            answer.goalNode = walker.goalNode;
            answer.fromNode = walker.fromNode;
            answer.door = static_cast<std::int16_t>(door);
            for (int k = 0; k < 3; ++k)
                answer.out[k] = walker.goal[k];
            break;
        }
        case kThinkDetour:
        {
            // (From where it stands: it waits, stopped, for the answer.)
            const ActorState &walker = a.states[index];
            answer.found = walkTriangulate(*w.world, &a, index, asked.small, walker.pos, asked.at,
                                           fxFromInt(asked.reach), fxFromInt(asked.least), answer.out);
            break;
        }
#ifndef HLDS_CHILD
        case kThinkLook:
        {
            // (From where it is now: it goes on walking meanwhile.)
            const ActorState &walker = a.states[index];
            answer.found = walkLookRound(*w.world, &a, index, asked.small, walker.pos, asked.at, fxFromInt(asked.reach),
                                         fxFromInt(asked.least), answer.out);
            break;
        }
#endif
        default:
            answer.found = false;
            break;
        }
        // (A map left meanwhile, or the question put away: nothing to give.)
        if (q.abort || slot.state != kThinkWorking)
            return true;
        answer.state = kThinkDone;
        slot = answer;
        return true;
    }

    /// Every question put away (a map left or loaded).
    inline void thinkClear(ThinkQueue &q)
    {
        for (int i = 0; i < q.count; ++i)
            q.slots[i] = ThinkSlot();
        q.next = 0;
    }
} // namespace hldata
