#include "apps/benchmark/latency_recorder.hpp"

#include <gtest/gtest.h>

#include <chrono>

using miniexchange::benchmark::LatencyRecorder;
using namespace std::chrono_literals;

// --- Edge cases: 0 samples ---

TEST(LatencyRecorderTest, EmptyRecorderReturnsZero) {
    LatencyRecorder recorder;
    EXPECT_EQ(recorder.count(), 0u);
    EXPECT_DOUBLE_EQ(recorder.avg_ns(), 0.0);
    EXPECT_DOUBLE_EQ(recorder.median_ns(), 0.0);
    EXPECT_DOUBLE_EQ(recorder.p99_ns(), 0.0);
    EXPECT_DOUBLE_EQ(recorder.max_ns(), 0.0);
}

// --- Edge case: 1 sample ---

TEST(LatencyRecorderTest, SingleSample) {
    LatencyRecorder recorder;
    recorder.record(std::chrono::nanoseconds{42});

    EXPECT_EQ(recorder.count(), 1u);
    EXPECT_DOUBLE_EQ(recorder.avg_ns(), 42.0);
    EXPECT_DOUBLE_EQ(recorder.median_ns(), 42.0);
    EXPECT_DOUBLE_EQ(recorder.p99_ns(), 42.0);
    EXPECT_DOUBLE_EQ(recorder.max_ns(), 42.0);
}

// --- Primary acceptance test: known sequence 1..100 ---
// Hand-computed expected values:
//   avg    = (1+2+...+100)/100 = 5050/100 = 50.5
//   median = (50+51)/2 = 50.5  (even count, average of two middle)
//   p99    = element at index ceil(0.99*100)-1 = index 98 = value 99
//   max    = 100

TEST(LatencyRecorderTest, KnownSequence1To100) {
    LatencyRecorder recorder;
    for (int i = 1; i <= 100; ++i) {
        recorder.record(std::chrono::nanoseconds{i});
    }

    EXPECT_EQ(recorder.count(), 100u);
    EXPECT_DOUBLE_EQ(recorder.avg_ns(), 50.5);
    EXPECT_DOUBLE_EQ(recorder.median_ns(), 50.5);
    EXPECT_DOUBLE_EQ(recorder.p99_ns(), 99.0);
    EXPECT_DOUBLE_EQ(recorder.max_ns(), 100.0);
}

// --- Odd count: median is the exact middle element ---
// Sequence 1..99: median = element at index 49 = 50
// avg = (1+2+...+99)/99 = 4950/99 = 50.0
// p99 = ceil(0.99*99)-1 = ceil(98.01)-1 = 99-1 = index 98 = value 99
// max = 99

TEST(LatencyRecorderTest, OddCount) {
    LatencyRecorder recorder;
    for (int i = 1; i <= 99; ++i) {
        recorder.record(std::chrono::nanoseconds{i});
    }

    EXPECT_EQ(recorder.count(), 99u);
    EXPECT_DOUBLE_EQ(recorder.avg_ns(), 50.0);
    EXPECT_DOUBLE_EQ(recorder.median_ns(), 50.0);
    EXPECT_DOUBLE_EQ(recorder.p99_ns(), 99.0);
    EXPECT_DOUBLE_EQ(recorder.max_ns(), 99.0);
}

// --- Unordered input: statistics should be identical regardless of insertion order ---

TEST(LatencyRecorderTest, UnorderedInput) {
    LatencyRecorder recorder;
    // Insert 1..100 in reverse order
    for (int i = 100; i >= 1; --i) {
        recorder.record(std::chrono::nanoseconds{i});
    }

    EXPECT_EQ(recorder.count(), 100u);
    EXPECT_DOUBLE_EQ(recorder.avg_ns(), 50.5);
    EXPECT_DOUBLE_EQ(recorder.median_ns(), 50.5);
    EXPECT_DOUBLE_EQ(recorder.p99_ns(), 99.0);
    EXPECT_DOUBLE_EQ(recorder.max_ns(), 100.0);
}

// --- Two samples: verifies even-count median averaging ---

TEST(LatencyRecorderTest, TwoSamples) {
    LatencyRecorder recorder;
    recorder.record(std::chrono::nanoseconds{10});
    recorder.record(std::chrono::nanoseconds{20});

    EXPECT_EQ(recorder.count(), 2u);
    EXPECT_DOUBLE_EQ(recorder.avg_ns(), 15.0);
    EXPECT_DOUBLE_EQ(recorder.median_ns(), 15.0);
    // p99: ceil(0.99*2)-1 = ceil(1.98)-1 = 2-1 = index 1 = value 20
    EXPECT_DOUBLE_EQ(recorder.p99_ns(), 20.0);
    EXPECT_DOUBLE_EQ(recorder.max_ns(), 20.0);
}

// --- All identical samples ---

TEST(LatencyRecorderTest, AllIdenticalSamples) {
    LatencyRecorder recorder;
    for (int i = 0; i < 50; ++i) {
        recorder.record(std::chrono::nanoseconds{7});
    }

    EXPECT_EQ(recorder.count(), 50u);
    EXPECT_DOUBLE_EQ(recorder.avg_ns(), 7.0);
    EXPECT_DOUBLE_EQ(recorder.median_ns(), 7.0);
    EXPECT_DOUBLE_EQ(recorder.p99_ns(), 7.0);
    EXPECT_DOUBLE_EQ(recorder.max_ns(), 7.0);
}
