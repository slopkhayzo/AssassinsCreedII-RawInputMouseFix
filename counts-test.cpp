// Tests ac2::PendingCounts: every count reaches the camera exactly once, except counts that
// waited too long. The first case guards against a real bug: with a coarse clock (GetTickCount
// advances every ~15.6 ms) a timestamp that read as "in the future" dropped every other tick
// of mouse input, which made the camera step.
#include <atomic>
#include <cstdio>
#include <thread>

#include "ac2_counts.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAILED: %s\n", what);
        ++g_failures;
    }
}

void CoarseClockKeepsEveryCount() {
    ac2::PendingCounts counts;
    long long sumX = 0;
    long long sumY = 0;
    long long dropped = 0;
    long long clock = 1000;
    for (int frame = 0; frame < 100000; ++frame) {
        // The clock stands still for several frames, then jumps by 15 or 16.
        if (frame % 8 == 0) clock += frame % 16 == 0 ? 15 : 16;
        counts.Add(3, -2, clock);
        long long x = 0;
        long long y = 0;
        if (!counts.Drain(clock, 250, x, y)) ++dropped;
        sumX += x;
        sumY += y;
    }
    Check(dropped == 0, "coarse clock: no count may be dropped");
    Check(sumX == 300000, "coarse clock: every x count arrives");
    Check(sumY == -200000, "coarse clock: every y count arrives");
}

void StaleCountsAreDropped() {
    ac2::PendingCounts counts;
    long long x = 0;
    long long y = 0;

    counts.Add(5, 7, 1000);
    counts.Add(1, 1, 1100);
    Check(counts.Drain(1250, 250, x, y) && x == 6 && y == 8, "exactly at the limit: kept");

    counts.Add(5, 7, 2000);
    Check(!counts.Drain(2251, 250, x, y) && x == 0 && y == 0, "past the limit: dropped");

    counts.Add(2, 3, 3000);
    Check(counts.Drain(3000, 250, x, y) && x == 2 && y == 3, "counts after a drop still arrive");

    Check(counts.Drain(9000, 250, x, y) && x == 0 && y == 0, "nothing pending: nothing happens");

    counts.Add(4, 4, 5000);
    Check(counts.Drain(4999, 250, x, y) && x == 4 && y == 4, "a slightly earlier clock reading: kept");
}

void ConcurrentThreadsKeepEveryCount() {
    ac2::FrameCounts counts;
    constexpr int kPolls = 2000000;
    std::atomic<bool> done{false};
    long long sumX = 0;
    long long sumY = 0;
    int state = 0;
    std::thread camera([&] {
        for (;;) {
            const bool finished = done.load();
            long long x = 0;
            long long y = 0;
            bool dropped = false;
            counts.Take(&state, 1, 1000000, x, y, dropped);
            sumX += x;
            sumY += y;
            if (finished) return;
        }
    });
    for (int index = 0; index < kPolls; ++index) counts.AddPoll(1, -1, 1);
    done.store(true);
    camera.join();
    // One last poll with no movement so the final counts belong to a frame not yet taken.
    counts.AddPoll(0, 0, 1);
    long long x = 0;
    long long y = 0;
    bool dropped = false;
    counts.Take(&state, 1, 1000000, x, y, dropped);
    sumX += x;
    sumY += y;
    Check(sumX == kPolls, "two threads: every x count arrives once");
    Check(sumY == -kPolls, "two threads: every y count arrives once");
}

void EachCameraStateGetsTheFrameOnce() {
    using Result = ac2::FrameCounts::Result;
    ac2::FrameCounts counts;
    int stateA = 0;
    int stateB = 0;
    long long x = 0;
    long long y = 0;
    bool dropped = false;

    counts.AddPoll(10, -4, 100);
    Check(counts.Take(&stateA, 100, 250, x, y, dropped) == Result::kFirstState && x == 10 && y == -4,
          "first state gets the frame");
    Check(counts.Take(&stateB, 100, 250, x, y, dropped) == Result::kOtherState && x == 10 && y == -4,
          "blend: a second state gets the same frame");
    Check(counts.Take(&stateA, 100, 250, x, y, dropped) == Result::kAlreadyServed && x == 0 && y == 0,
          "the same state never gets a frame twice");
    Check(counts.Take(&stateB, 100, 250, x, y, dropped) == Result::kAlreadyServed && x == 0 && y == 0,
          "the second state never gets a frame twice either");

    counts.AddPoll(1, 1, 110);
    Check(counts.Take(&stateA, 110, 250, x, y, dropped) == Result::kFirstState && x == 1 && y == 1,
          "next poll: new counts");

    counts.AddPoll(2, 0, 120);
    counts.AddPoll(3, 5, 121);
    Check(counts.Take(&stateA, 121, 250, x, y, dropped) == Result::kFirstState && x == 5 && y == 5,
          "two polls between updates: both are applied");

    counts.AddPoll(0, 0, 130);
    Check(counts.Take(&stateA, 130, 250, x, y, dropped) == Result::kFirstState && x == 0 && y == 0,
          "a poll with no movement: nothing to apply");

    counts.AddPoll(9, 9, 200);
    counts.AddPoll(0, 0, 900);
    Check(counts.Take(&stateA, 900, 250, x, y, dropped) == Result::kFirstState && x == 0 && y == 0 &&
              dropped,
          "too old: dropped and reported");

    counts.AddPoll(7, 7, 1000);
    counts.Discard(1000, 250);
    counts.AddPoll(0, 0, 1001);
    Check(counts.Take(&stateA, 1001, 250, x, y, dropped) == Result::kFirstState && x == 0 && y == 0,
          "discard forgets pending counts");

    counts.AddPoll(4, 4, 1100);
    Check(counts.Take(&stateA, 1100, 250, x, y, dropped) == Result::kFirstState && x == 4 && y == 4,
          "counts after a discard arrive");
    counts.Discard(1100, 250);
    Check(counts.Take(&stateB, 1100, 250, x, y, dropped) == Result::kOtherState && x == 0 && y == 0,
          "a discard also clears the current frame");
}

}  // namespace

int main() {
    CoarseClockKeepsEveryCount();
    StaleCountsAreDropped();
    ConcurrentThreadsKeepEveryCount();
    EachCameraStateGetsTheFrameOnce();
    if (g_failures != 0) {
        std::printf("%d count test(s) failed.\n", g_failures);
        return 1;
    }
    std::printf("Count tests passed.\n");
    return 0;
}
