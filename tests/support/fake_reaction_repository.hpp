#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "rtc/models/reaction.hpp"
#include "rtc/repositories/reaction_repository.hpp"
#include "rtc/utils/time.hpp"

namespace rtc::testing {

// In-memory IReactionRepository (one reaction per message+user).
class FakeReactionRepository final : public repositories::IReactionRepository {
  public:
    models::Reaction upsert(std::int64_t message_id,
                            std::int64_t user_id,
                            std::string_view emoji) override {
        for (auto& r : reactions_) {
            if (r.message_id == message_id && r.user_id == user_id) {
                r.emoji = std::string(emoji);
                r.updated_at = utils::now();
                return r;
            }
        }
        models::Reaction r;
        r.id = next_id_++;
        r.message_id = message_id;
        r.user_id = user_id;
        r.emoji = std::string(emoji);
        r.created_at = utils::now();
        r.updated_at = r.created_at;
        reactions_.push_back(r);
        return r;
    }

    bool remove(std::int64_t message_id, std::int64_t user_id) override {
        const auto before = reactions_.size();
        reactions_.erase(
            std::remove_if(
                reactions_.begin(),
                reactions_.end(),
                [&](const auto& r) { return r.message_id == message_id && r.user_id == user_id; }),
            reactions_.end());
        return reactions_.size() != before;
    }

    std::vector<models::Reaction> list_for_message(std::int64_t message_id) override {
        std::vector<models::Reaction> out;
        for (const auto& r : reactions_)
            if (r.message_id == message_id)
                out.push_back(r);
        return out;
    }

  private:
    std::vector<models::Reaction> reactions_;
    std::int64_t next_id_ = 1;
};

}  // namespace rtc::testing
