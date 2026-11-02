// Raw mouse counts travelling from the pad's poll (one thread) to the camera update.
//
// Every count must reach each camera state exactly once, except counts that waited longer than
// a limit: those were collected while the camera was not running (menus, cutscenes, other
// camera modes) and must not be replayed afterwards. Times are in the caller's clock units (the
// plugin uses QueryPerformanceCounter); 0 is reserved for "nothing pending".
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ac2 {

class PendingCounts {
public:
    void Add(long x, long y, long long now) {
        long long none = 0;
        since_.compare_exchange_strong(none, now);
        x_.fetch_add(x, std::memory_order_relaxed);
        y_.fetch_add(y, std::memory_order_relaxed);
    }

    // Takes everything pending into x and y. Returns false, with x and y zero, when the oldest
    // count waited longer than maxAge.
    bool Drain(long long now, long long maxAge, long long& x, long long& y) {
        const long long since = since_.exchange(0);
        x = x_.exchange(0);
        y = y_.exchange(0);
        if (since != 0 && now - since > maxAge && (x != 0 || y != 0)) {
            x = 0;
            y = 0;
            return false;
        }
        return true;
    }

private:
    std::atomic<long long> x_{0};
    std::atomic<long long> y_{0};
    std::atomic<long long> since_{0};
};

// Hands out one frame's counts per camera state. During a blend the game updates two cameras
// in one frame, and in the native path both see the same stick, so each distinct state gets
// the frame's counts. A state that asks again before the next poll gets nothing, so nothing is
// applied twice to the same yaw and pitch.
class FrameCounts {
public:
    enum class Result {
        kFirstState,    // first state of this frame
        kOtherState,    // another state in the same frame (a blend)
        kAlreadyServed  // this state already had this frame's counts: x and y are zero
    };

    // One successful poll. Zero movement counts too: it starts a new frame.
    void AddPoll(long x, long y, long long now) {
        if (x != 0 || y != 0) pending_.Add(x, y, now);
        polls_.fetch_add(1, std::memory_order_release);
    }

    // dropped is set when this call discarded counts as too old.
    Result Take(const void* state, long long now, long long maxAge, long long& x, long long& y,
                bool& dropped) {
        Lock();
        dropped = false;
        const std::uint32_t polls = polls_.load(std::memory_order_acquire);
        if (!hasFrame_ || polls != framePolls_) {
            dropped = !pending_.Drain(now, maxAge, frameX_, frameY_);
            framePolls_ = polls;
            hasFrame_ = true;
            servedCount_ = 0;
        }
        Result result = servedCount_ == 0 ? Result::kFirstState : Result::kOtherState;
        for (std::size_t index = 0; index < servedCount_; ++index) {
            if (served_[index] == state) {
                result = Result::kAlreadyServed;
                break;
            }
        }
        if (result == Result::kAlreadyServed) {
            x = 0;
            y = 0;
        } else {
            if (servedCount_ < kMaxStates) served_[servedCount_++] = state;
            x = frameX_;
            y = frameY_;
        }
        Unlock();
        return result;
    }

    // Forgets everything not yet applied (a toggle, or the camera locked).
    void Discard(long long now, long long maxAge) {
        Lock();
        pending_.Drain(now, maxAge, frameX_, frameY_);
        frameX_ = 0;
        frameY_ = 0;
        Unlock();
    }

private:
    static constexpr std::size_t kMaxStates = 8;

    void Lock() {
        while (lock_.test_and_set(std::memory_order_acquire)) {
        }
    }
    void Unlock() { lock_.clear(std::memory_order_release); }

    PendingCounts pending_;
    std::atomic<std::uint32_t> polls_{0};
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::uint32_t framePolls_ = 0;
    bool hasFrame_ = false;
    long long frameX_ = 0;
    long long frameY_ = 0;
    const void* served_[kMaxStates]{};
    std::size_t servedCount_ = 0;
};

}  // namespace ac2
