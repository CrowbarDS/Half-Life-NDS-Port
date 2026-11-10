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
        for (const Out &one : out_)
        {
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

    std::string Serve::statsLine(const Counts &before, unsigned seconds) const
    {
        const Counts &now = counts;
        char line[320];
        std::snprintf(line, sizeof(line),
                      "stats peers=%zu rooms=%zu in=%llu in_bytes=%llu out=%llu out_bytes=%llu drop_not_ours=%llu "
                      "drop_too_many=%llu send_failed=%llu full_rounds=%llu (last %u s)",
                      relay_.peers(), relay_.rooms(), static_cast<unsigned long long>(now.received - before.received),
                      static_cast<unsigned long long>(now.receivedBytes - before.receivedBytes),
                      static_cast<unsigned long long>(now.sent - before.sent),
                      static_cast<unsigned long long>(now.sentBytes - before.sentBytes),
                      static_cast<unsigned long long>(now.drops.notOurs - before.drops.notOurs),
                      static_cast<unsigned long long>(now.drops.tooMany - before.drops.tooMany),
                      static_cast<unsigned long long>(now.sendFailed - before.sendFailed),
                      static_cast<unsigned long long>(now.fullRounds - before.fullRounds), seconds);
        return line;
    }
} // namespace hlrelay
