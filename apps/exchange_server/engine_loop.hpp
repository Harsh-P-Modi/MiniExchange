#ifndef MINIEXCHANGE_APPS_EXCHANGE_SERVER_ENGINE_LOOP_HPP
#define MINIEXCHANGE_APPS_EXCHANGE_SERVER_ENGINE_LOOP_HPP

// engine_loop.hpp — the per-command body of the exchange server's engine
// thread, extracted from main() so it can be unit-tested without epoll,
// eventfd, or a live socket (which are Linux-only and gate the rest of
// this app out of non-Linux builds). This header is platform-neutral:
// it needs only core/ and interfaces/.
//
// Phase 11 R3: engine dispatch is wrapped in a try/catch at the loop
// boundary. The Charter says the engine "returns structured results,
// never throws for expected business outcomes" — so anything that DOES
// throw out of submit()/cancel() is an unexpected bug (an invariant
// violation, a std:: container throwing bad_alloc, etc.). Before this,
// such a throw propagated out of the engine thread uncaught and
// std::terminate-d the whole process, disconnecting every client over
// one client's pathological input. Catching here converts that into a
// single InternalError response to the offending client while the
// engine thread — and the venue — keep running.

#include <cstdio>
#include <exception>
#include <type_traits>
#include <variant>

#include "core/EngineCommand.hpp"
#include "core/Events.hpp"
#include "core/NewOrder.hpp"
#include "core/TaggedCommand.hpp"
#include "core/Types.hpp"
#include "interfaces/engine_api.hpp"

namespace miniexchange::exchange_server {

// The OrderId an EngineCommand refers to. All three alternatives
// (LimitOrder, MarketOrder, CancelRequest) expose `.id`.
[[nodiscard]] inline OrderId command_order_id(const EngineCommand& command) {
    return std::visit([](const auto& c) { return c.id; }, command);
}

// Dispatch one tagged command to the engine and return its response.
//
// On the happy path this is exactly the std::visit the engine thread
// ran inline before Phase 11: LimitOrder/MarketOrder → submit() (with
// the submitting client's id stamped as owner, for STP — Phase 8),
// CancelRequest → cancel().
//
// If dispatch throws, the exception is caught here (never escapes),
// logged to stderr, and turned into an InternalError response tagged to
// the originating order (R2 — the id is still recoverable from the
// command). `noexcept`: this function is the boundary; nothing past it
// should ever see an exception.
[[nodiscard]] inline EngineResponse dispatch_command(
    EngineAPI& engine, TaggedCommand& cmd) noexcept {
    try {
        return std::visit(
            [&engine, &cmd](auto& command) -> EngineResponse {
                using T = std::decay_t<decltype(command)>;
                if constexpr (std::is_same_v<T, LimitOrder> ||
                              std::is_same_v<T, MarketOrder>) {
                    command.owner = cmd.client;
                    return engine.submit(command);
                } else {
                    static_assert(std::is_same_v<T, CancelRequest>);
                    return engine.cancel(command.id);
                }
            },
            cmd.command);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "engine dispatch threw for client %llu: %s\n",
                     static_cast<unsigned long long>(cmd.client.value),
                     e.what());
    } catch (...) {
        std::fprintf(stderr,
                     "engine dispatch threw non-std exception for client %llu\n",
                     static_cast<unsigned long long>(cmd.client.value));
    }
    return EngineResponse{EngineResult::InternalError, {}, Quantity{0},
                          command_order_id(cmd.command)};
}

// WakeupCoalescer — Phase 11 R6. The engine thread notifies the I/O
// thread (an eventfd write — one syscall) that responses are ready.
// Before R6 that write fired once per response, inside the per-command
// loop body: one syscall per order regardless of how many responses were
// already queued. But the I/O thread's drain handler always drains the
// outbound queue to exhaustion in one call — so N wakeups and 1 wakeup
// flush the same responses. This coalesces the write to at most one per
// "drain the inbound queue until empty" cycle:
//
//   - after pushing each response:      note_response()
//   - when inbound.try_pop() fails
//     (queue about to go idle):         if (should_notify_on_idle()) write(eventfd)
//
// Under load (commands keep arriving) many responses are pushed before
// the queue ever reads empty, so their wakeups collapse into one. Under
// light load (one command, then idle) the queue reads empty immediately
// after the single response, so the wakeup still fires right away — no
// added latency, unlike a timer- or count-based batching window.
class WakeupCoalescer {
public:
    // Call once per response pushed to the outbound queue.
    void note_response() { pending_ = true; }

    // Call when the inbound queue is observed empty (the drain cycle is
    // ending). Returns true exactly when a wakeup should be issued now —
    // i.e. at least one response was pushed since the last wakeup — and
    // clears the pending flag so the next cycle starts fresh.
    bool should_notify_on_idle() {
        if (!pending_) return false;
        pending_ = false;
        return true;
    }

    [[nodiscard]] bool pending() const { return pending_; }

private:
    bool pending_ = false;
};

}  // namespace miniexchange::exchange_server

#endif  // MINIEXCHANGE_APPS_EXCHANGE_SERVER_ENGINE_LOOP_HPP
