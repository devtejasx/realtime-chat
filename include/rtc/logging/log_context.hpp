#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace rtc::logging {

// Per-thread correlation identity attached to every log line.
//
// Why a thread-local rather than a parameter
// ------------------------------------------
// Trace and span ids already come from tracing::current_span_context(), which is
// thread-local for the same reason: Crow handles a request on one thread, so a
// repository or cache call deep inside a handler can be correlated without
// threading a context object through every signature. The request id needs the
// same treatment — it is set once by the logging middleware and read by the
// formatter, so *existing* log statements gain correlation with no edit.
//
// That is the point. Making correlation opt-in per call site guarantees the
// lines that matter during an incident are the ones nobody remembered to
// annotate.
//
// This is thread-local, not global mutable state: each thread owns its value,
// there is no sharing and no synchronisation. It deliberately does not leak into
// work handed to the background executor or the cluster bus — those cross a
// thread boundary and must carry correlation explicitly.

// The calling thread's request id, or empty when none is set.
[[nodiscard]] std::string_view current_request_id() noexcept;

// Explicit set/clear, for callers that cannot hold an RAII guard.
//
// Crow requires a middleware's context to be move-assignable, which a scope
// guard cannot be — so LoggingMiddleware sets in before_handle and clears in
// after_handle instead. That is sound here because Crow always unwinds
// after_handle, including when a handler throws: the guarded property is that
// an id never outlives its request and mislabels the next one, and the unwind
// guarantees it. Prefer RequestIdScope anywhere that constraint does not apply.
void set_request_id(std::string request_id) noexcept;
void clear_request_id() noexcept;

// RAII scope for the request id. Restores the previous value on destruction, so
// nesting is safe and an early return cannot leave a stale id attached to
// whatever the thread handles next — which would mislabel every subsequent line
// in the worst possible way, by looking correct.
class RequestIdScope {
  public:
    explicit RequestIdScope(std::string request_id) noexcept;
    ~RequestIdScope();

    RequestIdScope(const RequestIdScope&) = delete;
    RequestIdScope& operator=(const RequestIdScope&) = delete;
    RequestIdScope(RequestIdScope&&) = delete;
    RequestIdScope& operator=(RequestIdScope&&) = delete;

  private:
    std::string previous_;
};

}  // namespace rtc::logging
