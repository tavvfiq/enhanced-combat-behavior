#include "DefenseTiming.h"
#include "Config.h"
#include "Logger.h"
#include "ThreadSafeMap.h"

#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace CombatAI::DefenseTiming
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        // One defense waiting for its blow
        struct Pending
        {
            RE::FormID target = 0;
            ActionType action = ActionType::None;
            float elapsed = 0.0f;   // seconds into the swing at commit
            float predicted = 0.0f; // expected seconds to the hit at commit
            Clock::time_point at{};
            bool debug = false;
        };
        ThreadSafeMap<RE::FormID, Pending> g_pending;

        enum Kind : std::size_t
        {
            kParry,
            kTimedBlock,
            kBash,
            kDodge,
            kKinds
        };
        constexpr std::array<const char *, kKinds> kNames{"parry", "timed block", "bash", "dodge"};

        struct Stats
        {
            int commits = 0, hit = 0, blocked = 0, noHit = 0, early = 0, inWindow = 0, late = 0;
            double sum = 0.0, sumSq = 0.0; // lead (s) of commits a blow resolved
        };
        std::mutex g_statsLock;
        std::array<Stats, kKinds> g_stats{};

        std::optional<Kind> KindOf(ActionType a_action)
        {
            switch (a_action) {
            case ActionType::Parry:
                return kParry;
            case ActionType::TimedBlock:
                return kTimedBlock;
            case ActionType::Bash:
                return kBash;
            case ActionType::Dodge:
                return kDodge;
            default:
                return std::nullopt;
            }
        }

        // The lead (seconds before the blow) each defense needs
        std::pair<float, float> Window(Kind a_kind)
        {
            const auto &config = Config::GetInstance();
            switch (a_kind) {
            case kTimedBlock:
                return {config.GetTimedBlock().timedBlockWindowStart, config.GetTimedBlock().timedBlockWindowEnd};
            case kDodge:
                return {0.0f, config.GetDodgeSystem().iFrameDuration};
            default: // a parry is a bash from the Block tree
                return {config.GetParry().parryWindowStart, config.GetParry().parryWindowEnd};
            }
        }

        // Records one resolved commit; a_elapsedAtHit < 0: no blow came
        void Resolve(RE::FormID a_self, const Pending &a_pending, float a_elapsedAtHit, bool a_blocked)
        {
            const auto kind = KindOf(a_pending.action);
            if (!kind) {
                return;
            }
            const auto [start, end] = Window(*kind);
            const bool hit = a_elapsedAtHit >= 0.0f;
            const float lead = hit ? a_elapsedAtHit - a_pending.elapsed : a_pending.predicted;
            const char *verdict = lead > end ? "early" : lead < start ? "late" : "in window";
            {
                std::scoped_lock lock(g_statsLock);
                auto &s = g_stats[*kind];
                ++s.commits;
                if (!hit) {
                    ++s.noHit;
                } else {
                    ++(a_blocked ? s.blocked : s.hit);
                    s.sum += lead;
                    s.sumSq += static_cast<double>(lead) * lead;
                    ++(lead > end ? s.early : lead < start ? s.late : s.inWindow);
                }
            }
            if (a_pending.debug) {
                if (hit) {
                    LOG_DEBUG("Timing 0x{:08X}: {} committed {:.2f}s into the swing, blow at {:.2f}s -> {:.2f}s before it "
                              "(predicted {:.2f}, window {:.2f}-{:.2f}: {}), {}",
                              a_self, kNames[*kind], a_pending.elapsed, a_elapsedAtHit, lead, a_pending.predicted, start,
                              end, verdict, a_blocked ? "blocked" : "took the hit");
                } else {
                    LOG_DEBUG("Timing 0x{:08X}: {} committed {:.2f}s into the swing, no blow came (dodged, missed or "
                              "cancelled; predicted {:.2f}s before it: {})",
                              a_self, kNames[*kind], a_pending.elapsed, a_pending.predicted, verdict);
                }
            }
        }
    } // namespace

    void OnCommit(RE::Actor *a_self, RE::FormID a_target, ActionType a_action, float a_elapsed, float a_predicted)
    {
        if (!a_self || !KindOf(a_action)) {
            return;
        }
        const RE::FormID id = a_self->GetFormID();
        if (const auto old = g_pending.Find(id)) {
            Resolve(id, *old, -1.0f, false); // a new commit before the last one's blow
        }
        g_pending.ModifyOrCreate(id, [&](Pending &p) {
            p = {a_target, a_action, a_elapsed, a_predicted, Clock::now(), Config::GetInstance().IsDebugTarget(id)};
        });
    }

    void OnHit(RE::Actor *a_victim, RE::FormID a_attacker, float a_elapsed, bool a_blocked)
    {
        if (!a_victim) {
            return;
        }
        const RE::FormID id = a_victim->GetFormID();
        const auto pending = g_pending.Find(id);
        if (!pending || pending->target != a_attacker) {
            return;
        }
        g_pending.Erase(id);
        Resolve(id, *pending, a_elapsed, a_blocked);
    }

    void Update(float a_deltaTime)
    {
        // Commits whose blow never came
        const auto now = Clock::now();
        std::vector<std::pair<RE::FormID, Pending>> expired;
        g_pending.WithWriteLock([&](auto &a_map) {
            for (auto it = a_map.begin(); it != a_map.end();) {
                if (now - it->second.at > std::chrono::milliseconds(1500)) {
                    expired.emplace_back(*it);
                    it = a_map.erase(it);
                } else {
                    ++it;
                }
            }
        });
        for (const auto &[id, pending] : expired) {
            Resolve(id, pending, -1.0f, false);
        }

        static float elapsed = 0.0f; // main thread only
        elapsed += a_deltaTime;
        if (elapsed < 60.0f) {
            return;
        }
        elapsed = 0.0f;
        std::array<Stats, kKinds> stats;
        {
            std::scoped_lock lock(g_statsLock);
            stats = g_stats;
            g_stats = {};
        }
        std::string report;
        for (std::size_t k = 0; k < kKinds; ++k) {
            const auto &s = stats[k];
            if (s.commits == 0) {
                continue;
            }
            const int resolved = s.hit + s.blocked;
            const double mean = resolved ? s.sum / resolved : 0.0;
            const double sd = resolved > 1 ? std::sqrt((std::max)(0.0, s.sumSq / resolved - mean * mean)) : 0.0;
            const auto [start, end] = Window(static_cast<Kind>(k));
            report += std::format("; {} {} (blocked {}, hit {}, no blow {}): {:.2f}s +- {:.2f} before the blow, window "
                                  "{:.2f}-{:.2f}: early {} in {} late {}",
                                  kNames[k], s.commits, s.blocked, s.hit, s.noHit, mean, sd, start, end, s.early,
                                  s.inWindow, s.late);
        }
        if (!report.empty()) {
            LOG_INFO("Defense timing (last 60s, all NPCs){}", report);
        }
    }

    void Evict(RE::FormID a_formID)
    {
        g_pending.Erase(a_formID);
    }
} // namespace CombatAI::DefenseTiming
