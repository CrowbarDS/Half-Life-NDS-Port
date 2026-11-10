#include "Serve.hpp"

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
            ++received;
            relay_.receive(from, bytes_, size, nowMs, out_);
        }
        const bool full = read == kMostPerRound;
        if (full)
            ++fullRounds;
        if (nowMs - lastTick_ >= kTickMs)
        {
            relay_.tick(nowMs, out_);
            lastTick_ = nowMs;
        }
        for (const Out &one : out_)
        {
            if (socket_.send(one.to, one.bytes.data(), static_cast<int>(one.bytes.size())))
                ++sent;
            else
                ++sendFailed;
        }
        out_.clear();
        return full;
    }
} // namespace hlrelay
