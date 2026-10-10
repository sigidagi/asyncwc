#include "asyncwc/backoff.hpp"

#include <gtest/gtest.h>

namespace asyncwc {
namespace {

using std::chrono::milliseconds;

ReconnectPolicy policy_without_jitter() {
    ReconnectPolicy policy;
    policy.initial_delay = milliseconds{100};
    policy.max_delay = milliseconds{1000};
    policy.multiplier = 2.0;
    policy.jitter = 0.0;
    return policy;
}

TEST(BackoffTest, GrowsExponentiallyUpToTheCap) {
    Backoff backoff{policy_without_jitter()};
    EXPECT_EQ(backoff.next(), milliseconds{100});
    EXPECT_EQ(backoff.next(), milliseconds{200});
    EXPECT_EQ(backoff.next(), milliseconds{400});
    EXPECT_EQ(backoff.next(), milliseconds{800});
    EXPECT_EQ(backoff.next(), milliseconds{1000});
    EXPECT_EQ(backoff.next(), milliseconds{1000});
    EXPECT_EQ(backoff.attempts(), 6U);
}

TEST(BackoffTest, MultiplierOfOneKeepsTheDelayConstant) {
    auto policy = policy_without_jitter();
    policy.multiplier = 1.0;
    Backoff backoff{policy};
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(backoff.next(), milliseconds{100});
    }
}

TEST(BackoffTest, StopsAfterMaxAttempts) {
    auto policy = policy_without_jitter();
    policy.max_attempts = 2;
    Backoff backoff{policy};
    EXPECT_TRUE(backoff.next());
    EXPECT_TRUE(backoff.next());
    EXPECT_FALSE(backoff.next());
    EXPECT_FALSE(backoff.next());
    EXPECT_EQ(backoff.attempts(), 2U);
}

TEST(BackoffTest, ResetStartsOver) {
    auto policy = policy_without_jitter();
    policy.max_attempts = 1;
    Backoff backoff{policy};
    EXPECT_EQ(backoff.next(), milliseconds{100});
    EXPECT_FALSE(backoff.next());
    backoff.reset();
    EXPECT_EQ(backoff.attempts(), 0U);
    EXPECT_EQ(backoff.next(), milliseconds{100});
}

TEST(BackoffTest, JitterStaysWithinBoundsAndCap) {
    auto policy = policy_without_jitter();
    policy.jitter = 0.5;
    Backoff backoff{policy, 42};
    bool varied = false;
    for (int round = 0; round < 50; ++round) {
        backoff.reset();
        const auto first = backoff.next().value();
        EXPECT_GE(first, milliseconds{50});
        EXPECT_LE(first, milliseconds{150});
        varied = varied || first != milliseconds{100};
        for (int i = 0; i < 6; ++i) {
            EXPECT_LE(backoff.next().value(), milliseconds{1000});
        }
    }
    EXPECT_TRUE(varied);
}

TEST(BackoffTest, SameSeedGivesSameSequence) {
    auto policy = policy_without_jitter();
    policy.jitter = 0.3;
    Backoff a{policy, 7};
    Backoff b{policy, 7};
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(a.next(), b.next());
    }
}

TEST(BackoffTest, ZeroInitialDelayIsAllowed) {
    auto policy = policy_without_jitter();
    policy.initial_delay = milliseconds{0};
    Backoff backoff{policy};
    EXPECT_EQ(backoff.next(), milliseconds{0});
    EXPECT_EQ(backoff.next(), milliseconds{0});
}

} // namespace
} // namespace asyncwc
