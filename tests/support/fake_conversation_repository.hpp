#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "rtc/errors/exceptions.hpp"
#include "rtc/models/conversation.hpp"
#include "rtc/models/conversation_participant.hpp"
#include "rtc/repositories/conversation_repository.hpp"
#include "rtc/utils/time.hpp"

namespace rtc::testing {

// In-memory IConversationRepository mirroring the SQL semantics (direct dedup,
// per-conversation unique membership, ownership) for fast service unit tests.
class FakeConversationRepository final : public repositories::IConversationRepository {
  public:
    models::Conversation create_or_get_direct(std::int64_t a, std::int64_t b) override {
        const std::string key = direct_key(a, b);
        for (const auto& c : conversations_) {
            if (c.direct_key == key) {
                ensure_participant(c.id, a, models::ParticipantRole::kMember);
                ensure_participant(c.id, b, models::ParticipantRole::kMember);
                return c;
            }
        }
        models::Conversation c;
        c.id = next_conv_id_++;
        c.type = models::ConversationType::kDirect;
        c.direct_key = key;
        c.created_at = utils::now();
        c.updated_at = c.created_at;
        conversations_.push_back(c);
        ensure_participant(c.id, a, models::ParticipantRole::kMember);
        ensure_participant(c.id, b, models::ParticipantRole::kMember);
        return c;
    }

    models::Conversation create_group(std::int64_t owner_id,
                                      std::string_view name,
                                      const std::vector<std::int64_t>& members) override {
        models::Conversation c;
        c.id = next_conv_id_++;
        c.type = models::ConversationType::kGroup;
        c.name = std::string(name);
        c.owner_id = owner_id;
        c.created_at = utils::now();
        c.updated_at = c.created_at;
        conversations_.push_back(c);
        ensure_participant(c.id, owner_id, models::ParticipantRole::kOwner);
        for (const auto m : members) {
            if (m != owner_id)
                ensure_participant(c.id, m, models::ParticipantRole::kMember);
        }
        return c;
    }

    std::optional<models::Conversation> find_by_id(std::int64_t id) override {
        for (const auto& c : conversations_)
            if (c.id == id)
                return c;
        return std::nullopt;
    }

    std::vector<models::Conversation> list_for_user(std::int64_t user_id,
                                                    const dto::Pagination&) override {
        std::vector<models::Conversation> out;
        for (const auto& c : conversations_)
            if (is_participant(c.id, user_id))
                out.push_back(c);
        return out;
    }

    std::vector<models::ConversationParticipant> list_participants(std::int64_t conv) override {
        std::vector<models::ConversationParticipant> out;
        for (const auto& p : participants_)
            if (p.conversation_id == conv)
                out.push_back(p);
        return out;
    }

    std::vector<std::int64_t> list_participant_ids(std::int64_t conv) override {
        std::vector<std::int64_t> out;
        for (const auto& p : participants_)
            if (p.conversation_id == conv)
                out.push_back(p.user_id);
        return out;
    }

    std::vector<std::int64_t> list_conversation_ids(std::int64_t user_id) override {
        std::vector<std::int64_t> out;
        for (const auto& p : participants_)
            if (p.user_id == user_id)
                out.push_back(p.conversation_id);
        return out;
    }

    std::vector<std::int64_t> list_peer_ids(std::int64_t user_id) override {
        std::set<std::int64_t> peers;
        for (const auto& p1 : participants_) {
            if (p1.user_id != user_id)
                continue;
            for (const auto& p2 : participants_)
                if (p2.conversation_id == p1.conversation_id && p2.user_id != user_id)
                    peers.insert(p2.user_id);
        }
        return {peers.begin(), peers.end()};
    }

    std::optional<models::ConversationParticipant> find_participant(std::int64_t conv,
                                                                    std::int64_t user) override {
        for (const auto& p : participants_)
            if (p.conversation_id == conv && p.user_id == user)
                return p;
        return std::nullopt;
    }

    bool is_participant(std::int64_t conv, std::int64_t user) override {
        return find_participant(conv, user).has_value();
    }

    void add_participant(std::int64_t conv,
                         std::int64_t user,
                         models::ParticipantRole role) override {
        if (is_participant(conv, user)) {
            throw rtc::errors::ConflictException("User is already a member");
        }
        ensure_participant(conv, user, role);
    }

    void remove_participant(std::int64_t conv, std::int64_t user) override {
        participants_.erase(
            std::remove_if(
                participants_.begin(),
                participants_.end(),
                [&](const auto& p) { return p.conversation_id == conv && p.user_id == user; }),
            participants_.end());
    }

    void rename(std::int64_t conv, std::string_view name) override {
        for (auto& c : conversations_)
            if (c.id == conv)
                c.name = std::string(name);
    }

    void transfer_ownership(std::int64_t conv, std::int64_t new_owner) override {
        for (auto& c : conversations_)
            if (c.id == conv)
                c.owner_id = new_owner;
        for (auto& p : participants_) {
            if (p.conversation_id != conv)
                continue;
            p.role = (p.user_id == new_owner) ? models::ParticipantRole::kOwner
                                              : models::ParticipantRole::kMember;
        }
    }

    void remove(std::int64_t conv) override {
        conversations_.erase(std::remove_if(conversations_.begin(),
                                            conversations_.end(),
                                            [&](const auto& c) { return c.id == conv; }),
                             conversations_.end());
        participants_.erase(
            std::remove_if(participants_.begin(),
                           participants_.end(),
                           [&](const auto& p) { return p.conversation_id == conv; }),
            participants_.end());
    }

    void update_last_read(std::int64_t conv, std::int64_t user, std::int64_t message_id) override {
        for (auto& p : participants_) {
            if (p.conversation_id == conv && p.user_id == user) {
                if (!p.last_read_message_id || *p.last_read_message_id < message_id) {
                    p.last_read_message_id = message_id;
                }
            }
        }
    }

  private:
    static std::string direct_key(std::int64_t a, std::int64_t b) {
        return std::to_string(std::min(a, b)) + ":" + std::to_string(std::max(a, b));
    }

    void ensure_participant(std::int64_t conv, std::int64_t user, models::ParticipantRole role) {
        if (is_participant(conv, user))
            return;
        models::ConversationParticipant p;
        p.id = next_part_id_++;
        p.conversation_id = conv;
        p.user_id = user;
        p.role = role;
        p.joined_at = utils::now();
        participants_.push_back(p);
    }

    std::vector<models::Conversation> conversations_;
    std::vector<models::ConversationParticipant> participants_;
    std::int64_t next_conv_id_ = 1;
    std::int64_t next_part_id_ = 1;
};

}  // namespace rtc::testing
