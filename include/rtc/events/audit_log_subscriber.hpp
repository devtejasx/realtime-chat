#pragma once

#include <string_view>

#include "rtc/events/event_bus.hpp"
#include "rtc/features/feature_flags.hpp"
#include "rtc/services/audit_service.hpp"

namespace rtc::events {

// Bridges the domain event bus to audit persistence.
//
// This is the shape every subscriber should take: it owns no policy of its own,
// it just connects one side of the bus to one collaborator. The decision about
// *which* events are auditable lives in AuditService (is_auditable), and the
// decision about whether auditing is on at all lives in the feature flags —
// so this class has exactly one job and stays trivially testable.
//
// Failures are allowed to propagate: EventDispatcher isolates and logs them, and
// swallowing them here would hide an audit outage, which is the last thing you
// want to be quiet about.
class AuditLogSubscriber final : public IEventSubscriber {
public:
    AuditLogSubscriber(services::AuditService& audit, const features::FeatureFlags& flags) noexcept
        : audit_(audit), flags_(flags) {}

    [[nodiscard]] bool interested_in(EventType type) const noexcept override {
        return flags_.is_enabled(features::Feature::kAuditLog) && services::is_auditable(type);
    }

    void handle(const DomainEvent& event) override { (void) audit_.record(event); }

    [[nodiscard]] std::string_view subscriber_name() const noexcept override {
        return "audit_log";
    }

private:
    services::AuditService& audit_;
    const features::FeatureFlags& flags_;
};

}  // namespace rtc::events
