// WebSocket protocol encoding and broadcast fan-out.
//
// This is the throughput-critical path: every message sent produces one encode per
// distinct protocol version among the recipients, plus one socket write per
// recipient. The numbers here set the ceiling on messages/second per instance.
#include <benchmark/benchmark.h>

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "rtc/realtime/protocol.hpp"

namespace {

using rtc::realtime::protocol::Envelope;
using rtc::realtime::protocol::Version;

[[nodiscard]] nlohmann::json make_message_payload(std::size_t content_length) {
    return nlohmann::json{
        {"id", 123456},
        {"conversation_id", 42},
        {"sender_id", 7},
        {"type", "text"},
        {"content", std::string(content_length, 'x')},
        {"created_at", "2026-07-30T12:00:00Z"},
        {"attachment_ids", nlohmann::json::array()},
    };
}

// v1 encode across realistic message sizes. JSON serialisation cost is roughly
// linear in payload size, so the argument sweep shows where that starts to matter.
void BM_EncodeV1(benchmark::State& state) {
    const auto payload = make_message_payload(static_cast<std::size_t>(state.range(0)));
    for (auto _ : state) {
        benchmark::DoNotOptimize(
            rtc::realtime::protocol::encode(Version::kV1, "message.created", payload));
    }
    state.SetItemsProcessed(state.iterations());
    // Both operands are signed 64-bit; the cast keeps -Wsign-conversion quiet
    // without changing the value.
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) * state.range(0));
}
BENCHMARK(BM_EncodeV1)->Arg(16)->Arg(256)->Arg(4096);

// v2 encode. Costs a little more than v1 — an ISO-8601 timestamp is formatted and
// three extra keys are written. The gap is the price of the richer envelope, and is
// why v2 is negotiated rather than emitted unconditionally.
void BM_EncodeV2(benchmark::State& state) {
    const auto payload = make_message_payload(static_cast<std::size_t>(state.range(0)));
    const Envelope envelope{"req-abc123", "corr-def456"};
    for (auto _ : state) {
        benchmark::DoNotOptimize(
            rtc::realtime::protocol::encode(Version::kV2, "message.created", payload, envelope));
    }
    state.SetItemsProcessed(state.iterations());
    // Both operands are signed 64-bit; the cast keeps -Wsign-conversion quiet
    // without changing the value.
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) * state.range(0));
}
BENCHMARK(BM_EncodeV2)->Arg(16)->Arg(256)->Arg(4096);

// Decoding an inbound client frame: parse plus event dispatch lookup.
void BM_DecodeV1(benchmark::State& state) {
    const std::string frame =
        R"({"type":"message.send","data":{"conversation_id":42,"content":"hello there"}})";
    for (auto _ : state) {
        benchmark::DoNotOptimize(rtc::realtime::protocol::decode(frame));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_DecodeV1);

void BM_DecodeV2(benchmark::State& state) {
    const std::string frame =
        R"({"event":"message.send","payload":{"conversation_id":42,"content":"hello there"},)"
        R"("request_id":"req-1","correlation_id":"corr-1"})";
    for (auto _ : state) {
        benchmark::DoNotOptimize(rtc::realtime::protocol::decode(frame));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_DecodeV2);

// A malformed frame must be cheap: it is unauthenticated-ish traffic a client can
// send freely, so a costly rejection would be a denial-of-service lever. The
// non-throwing parse is what keeps this comparable to the success path.
void BM_DecodeMalformed(benchmark::State& state) {
    const std::string frame = R"({"type":)";  // truncated
    for (auto _ : state) {
        benchmark::DoNotOptimize(rtc::realtime::protocol::decode(frame));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_DecodeMalformed);

// Simulates the broadcast fan-out strategy.
//
// The connection manager encodes once per *distinct version* among recipients (at
// most twice) and then reuses the buffer for every socket write. This measures that
// against the naive alternative of encoding per recipient, which is the mistake the
// FrameCache exists to avoid.
void BM_FanOutEncodeOncePerVersion(benchmark::State& state) {
    const auto payload = make_message_payload(256);
    const auto recipients = static_cast<std::size_t>(state.range(0));

    for (auto _ : state) {
        // One encode, N reuses.
        const std::string frame =
            rtc::realtime::protocol::encode(Version::kV1, "message.created", payload);
        for (std::size_t i = 0; i < recipients; ++i) {
            benchmark::DoNotOptimize(frame.data());
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(recipients));
}
BENCHMARK(BM_FanOutEncodeOncePerVersion)->Arg(2)->Arg(10)->Arg(100)->Arg(1000);

void BM_FanOutEncodePerRecipient(benchmark::State& state) {
    const auto payload = make_message_payload(256);
    const auto recipients = static_cast<std::size_t>(state.range(0));

    for (auto _ : state) {
        for (std::size_t i = 0; i < recipients; ++i) {
            benchmark::DoNotOptimize(
                rtc::realtime::protocol::encode(Version::kV1, "message.created", payload));
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(recipients));
}
BENCHMARK(BM_FanOutEncodePerRecipient)->Arg(2)->Arg(10)->Arg(100)->Arg(1000);

}  // namespace
