#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace g_Hack {
    struct GachaStack { uint64_t ID; int Quantity; };

    class GachaOpenScheduler {
        struct State {
            int Quantity = 0, Pending = 0;
            unsigned Stalls = 0;
            uint64_t RetryAt = 0;
            bool Seen = false;
        };
        std::unordered_map<uint64_t, State> States;
        std::vector<uint64_t> Order;
        size_t Cursor = 0;
        uint64_t NextBatchAt = 0;
    public:
        static constexpr int BatchLimit = 32, PendingLimit = 64;

        void Observe(const std::vector<GachaStack>& Stacks, uint64_t Now) {
            for (auto& [ID, Entry] : States) Entry.Seen = false;
            Order.clear();
            for (const auto& Stack : Stacks) {
                if (Stack.Quantity <= 0) continue;
                auto& Entry = States[Stack.ID];
                if (Entry.Seen) continue; // Duplicate inventory entries never double the budget.
                Entry.Seen = true; Order.push_back(Stack.ID);
                if (Stack.Quantity < Entry.Quantity) {
                    Entry.Pending = std::max(0, Entry.Pending - (Entry.Quantity - Stack.Quantity));
                    Entry.Stalls = 0;
                    Entry.RetryAt = Now + 2000;
                }
                Entry.Quantity = Stack.Quantity;
                Entry.Pending = std::min(Entry.Pending, Entry.Quantity);
                if (Entry.Pending && Now >= Entry.RetryAt) {
                    Entry.Pending = 0;
                    Entry.Stalls = std::min(Entry.Stalls + 1, 3u);
                }
            }
            std::erase_if(States, [](const auto& Pair) { return !Pair.second.Seen; });
            Cursor = Order.empty() ? 0 : Cursor % Order.size();
        }

        std::vector<uint64_t> Plan(uint64_t Now, float IntervalMs) {
            std::vector<uint64_t> Requests;
            if (Order.empty() || Now < NextBatchAt) return Requests;
            const auto Interval = std::isfinite(IntervalMs) ? std::clamp(IntervalMs, 10.f, 1000.f) : 50.f;
            NextBatchAt = Now + static_cast<uint64_t>(std::lround(Interval));
            int Capacity = std::min(BatchLimit, PendingLimit - Pending());
            // Rotate across item IDs so a rejected stack cannot block the rest.
            size_t WithoutProgress = 0;
            while (Capacity > 0 && WithoutProgress < Order.size()) {
                const auto ID = Order[Cursor];
                Cursor = (Cursor + 1) % Order.size();
                auto& Entry = States.at(ID);
                if (Entry.Pending >= Entry.Quantity || (Entry.Stalls && Entry.Pending)) {
                    ++WithoutProgress; continue;
                }
                if (!Entry.Pending) Entry.RetryAt = Now + (2000ULL << Entry.Stalls);
                ++Entry.Pending; Requests.push_back(ID); --Capacity; WithoutProgress = 0;
            }
            return Requests;
        }
        void Unsent(uint64_t ID) {
            const auto It = States.find(ID);
            if (It != States.end()) It->second.Pending = std::max(0, It->second.Pending - 1);
        }
        int Pending() const {
            int Count = 0;
            for (const auto& [ID, Entry] : States) Count += Entry.Pending;
            return Count;
        }
        uint64_t Remaining() const {
            uint64_t Count = 0;
            for (const auto& [ID, Entry] : States) Count += static_cast<uint64_t>(Entry.Quantity);
            return Count;
        }
        void Reset() { *this = {}; }
    };
}
