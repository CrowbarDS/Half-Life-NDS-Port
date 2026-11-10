#include "Serve.hpp"

#include <cstdio>

namespace hlrelay
{
    bool Serve::round(std::uint32_t nowMs)
    {
        int read = 0;
        for (; read < kMostPerRound; ++read)
        {
            Address from;
            const int size = socket_.receive(from, bytes_, static_cast<int>(sizeof(bytes_)));
            if (size <= 0)
                break;
            ++counts.received;
            counts.receivedBytes += static_cast<std::uint64_t>(size);
            relay_.receive(from, bytes_, size, nowMs, out_);
        }
        const bool full = read == kMostPerRound;
        if (full)
            ++counts.fullRounds;
        if (nowMs - lastTick_ >= kTickMs)
        {
            relay_.tick(nowMs, out_);
            lastTick_ = nowMs;
        }
        const Limits &limits = relay_.limits;
        for (const Out &one : out_)
        {
            if (!outBytes_.take(static_cast<std::uint32_t>(one.bytes.size()), limits.outBytesPerSecond,
                                limits.outBytesPerSecond, nowMs))
            {
                ++counts.outBudget;
                continue;
            }
            if (socket_.send(one.to, one.bytes.data(), static_cast<int>(one.bytes.size())))
            {
                ++counts.sent;
                counts.sentBytes += one.bytes.size();
            }
            else
                ++counts.sendFailed;
        }
        out_.clear();
        counts.drops = relay_.drops();
        if (nowMs - lastStats_ >= kStatsMs)
        {
            if (relay_.log)
                relay_.log(statsLine(atLastStats_, (nowMs - lastStats_) / 1000));
            atLastStats_ = counts;
            lastStats_ = nowMs;
        }
        return full;
    }

    void Serve::stop()
    {
        relay_.stop(out_);
        for (const Out &one : out_)
            socket_.send(one.to, one.bytes.data(), static_cast<int>(one.bytes.size()));
        out_.clear();
    }

    std::string Serve::statsLine(const Counts &before, unsigned seconds) const
    {
        const Counts &now = counts;
        const Drops &d = now.drops, &b = before.drops;
        auto n = [](std::uint64_t a, std::uint64_t z) { return static_cast<unsigned long long>(a - z); };
        char line[640];
        std::snprintf(line, sizeof(line),
                      "stats peers=%zu rooms=%zu in=%llu in_bytes=%llu out=%llu out_bytes=%llu drop_not_ours=%llu "
                      "other_version=%llu resumed=%llu drop_too_many=%llu drop_unknown=%llu drop_bad_cookie=%llu "
                      "drop_rate_hello=%llu drop_rate_control=%llu drop_rate_data=%llu drop_rate_voice=%llu "
                      "drop_cap_peers=%llu drop_cap_rooms=%llu drop_cap_knocks=%llu drop_out_budget=%llu "
                      "send_failed=%llu full_rounds=%llu (last %u s)",
                      relay_.peers(), relay_.rooms(), n(now.received, before.received),
                      n(now.receivedBytes, before.receivedBytes), n(now.sent, before.sent),
                      n(now.sentBytes, before.sentBytes), n(d.notOurs, b.notOurs), n(d.otherVersion, b.otherVersion),
                      n(d.resumed, b.resumed),
                      n(d.tooMany, b.tooMany), n(d.unknown, b.unknown), n(d.badCookie, b.badCookie),
                      n(d.rateHello, b.rateHello), n(d.rateControl, b.rateControl), n(d.rateData, b.rateData),
                      n(d.rateVoice, b.rateVoice), n(d.capPeers, b.capPeers), n(d.capRooms, b.capRooms),
                      n(d.capKnocks, b.capKnocks), n(now.outBudget, before.outBudget),
                      n(now.sendFailed, before.sendFailed), n(now.fullRounds, before.fullRounds), seconds);
        return line;
    }
} // namespace hlrelay
