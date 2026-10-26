#include "Intents.h"
#include "Config.h"
#include "Logger.h"
#include "ThreadSafeMap.h"

namespace CombatAI::Intents
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        using Seconds = std::chrono::duration<float>;

        // How long an untaken intent stays valid. The tree re-checks every 0.05 s when idle,
        // so these only cover the wait for a running node (e.g. an earlier swing) to finish.
        constexpr Seconds kAttackTTL{0.5f};
        constexpr Seconds kDefendTTL{0.3f};
        // Movement is refreshed by every brain decision (~0.1 s); a little slack covers skipped ticks.
        constexpr Seconds kMoveTTL{0.4f};
        // How long a taken attack kind steers the attack-chance hook (StartAttack runs right
        // after the branch is taken; this only has to outlast that one call).
        constexpr Seconds kCommitWindow{1.0f};

        struct Entry
        {
            Attack attack = Attack::None;
            Clock::time_point attackUntil{};
            Defend defend = Defend::None;
            Clock::time_point defendUntil{};
            Attack committed = Attack::None;
            Clock::time_point committedUntil{};
            Move move = Move::None;
            Clock::time_point moveUntil{};
            float param = 0.0f; // Circle side or Advance stop distance (see PostMove)
            Clock::time_point dualBlockUntil{};
            float shotFactor = 1.0f;
            Clock::time_point shotUntil{};
            Clock::time_point holdSince{}; // start of the current shot hold (epoch = not holding)
            float refusedSide = 0.0f;      // Circle side that last failed, and when
            Clock::time_point refusedAt{};
            Clock::time_point attackVetoUntil{};
            Clock::time_point swingAt{};
            bool dualBlockStarted = false;
        };

        ThreadSafeMap<RE::FormID, Entry> g_board;

        // Feedback counters since the last summary (atomics: posted on the brain thread, taken on tree threads)
        struct Counters
        {
            std::atomic<std::uint32_t> attackPosted[3]{}, attackTaken[3]{}, attackMatched[3]{};
            std::atomic<std::uint32_t> defendPosted[3]{}, defendTaken[3]{};
            std::atomic<std::uint32_t> moveChanged[5]{};
            std::atomic<std::uint32_t> preempted[2]{}; // [0] our branch's node, [1] a vanilla node
        } g_count;
        // Actors whose taken attack already counted as matched (one match per take)
        ThreadSafeMap<RE::FormID, bool> g_matched;
        // Last movement branch each actor's tree entered (debug log only, so we see when it changes)
        ThreadSafeMap<RE::FormID, Move> g_moveRun;
        ThreadSafeMap<RE::FormID, std::uint32_t> g_defendExpired; // per actor, since the last summary

        // Block/bash requests in a row the tree never took, and when the latest expired (DefendUnavailable)
        struct Refusals
        {
            std::uint32_t streak = 0;
            Clock::time_point last{};
        };
        ThreadSafeMap<RE::FormID, Refusals> g_defendRefusals;
        constexpr std::uint32_t kRefusalStreak = 3;
        constexpr Seconds kRefusalHold{10.0f};

        void Debug(RE::Actor *a_actor, const char *a_what, int a_kind)
        {
            if (Config::GetInstance().IsDebugTarget(a_actor->GetFormID())) {
                LOG_DEBUG("Intent 0x{:08X}: {} {}", static_cast<std::uint32_t>(a_actor->GetFormID()), a_what, a_kind);
            }
        }
    } // namespace

    bool PostAttack(RE::Actor *a_actor, Attack a_kind)
    {
        if (!a_actor) {
            return false;
        }
        const auto now = Clock::now();
        bool posted = false;
        g_board.ModifyOrCreate(a_actor->GetFormID(), [&](Entry &e) {
            if (e.attack == a_kind && now < e.attackUntil) {
                return; // still waiting for the tree
            }
            e.attack = a_kind;
            e.attackUntil = now + std::chrono::duration_cast<Clock::duration>(kAttackTTL);
            posted = true;
        });
        if (posted) {
            g_count.attackPosted[static_cast<int>(a_kind)].fetch_add(1, std::memory_order_relaxed);
            Debug(a_actor, "post attack", static_cast<int>(a_kind));
        }
        return posted;
    }

    bool PostDefend(RE::Actor *a_actor, Defend a_kind)
    {
        if (!a_actor) {
            return false;
        }
        const auto now = Clock::now();
        bool posted = false;
        g_board.ModifyOrCreate(a_actor->GetFormID(), [&](Entry &e) {
            if (e.defend == a_kind && now < e.defendUntil) {
                return; // still waiting for the tree
            }
            if (e.defend != Defend::None) {
                g_defendExpired.ModifyOrCreate(a_actor->GetFormID(), [](std::uint32_t &n) { ++n; }); // never taken
                g_defendRefusals.ModifyOrCreate(a_actor->GetFormID(), [&](Refusals &r) {
                    ++r.streak;
                    r.last = now;
                });
            }
            e.defend = a_kind;
            e.defendUntil = now + std::chrono::duration_cast<Clock::duration>(kDefendTTL);
            posted = true;
        });
        if (posted) {
            g_count.defendPosted[static_cast<int>(a_kind)].fetch_add(1, std::memory_order_relaxed);
            Debug(a_actor, "post defend", static_cast<int>(a_kind));
        }
        return posted;
    }

    Attack TakeAttack(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return Attack::None;
        }
        Attack taken = Attack::None;
        const auto now = Clock::now();
        g_board.Modify(a_actor->GetFormID(), [&](Entry &e) {
            if (e.attack != Attack::None && now < e.attackUntil) {
                taken = e.attack;
                e.committed = taken;
                e.committedUntil = now + std::chrono::duration_cast<Clock::duration>(kCommitWindow);
            }
            e.attack = Attack::None;
        });
        if (taken != Attack::None) {
            g_count.attackTaken[static_cast<int>(taken)].fetch_add(1, std::memory_order_relaxed);
            g_matched.ModifyOrCreate(a_actor->GetFormID(), [](bool &m) { m = false; });
            Debug(a_actor, "tree took attack", static_cast<int>(taken));
        }
        return taken;
    }

    bool TakeDefend(RE::Actor *a_actor, Defend a_kind)
    {
        if (!a_actor) {
            return false;
        }
        bool taken = false;
        const auto now = Clock::now();
        g_board.Modify(a_actor->GetFormID(), [&](Entry &e) {
            if (e.defend == a_kind && now < e.defendUntil) {
                taken = true;
                e.defend = Defend::None;
                // A bash right after a swing: drop that swing's commit, or the attack-kind filter zeroes every bash
                // candidate (run 58: the chief's bashes taken 35/35, no bashStart; "intent: other kind")
                if (a_kind == Defend::Bash) {
                    e.committed = Attack::None;
                }
            }
        });
        if (taken) {
            g_defendRefusals.Erase(a_actor->GetFormID());
            g_count.defendTaken[static_cast<int>(a_kind)].fetch_add(1, std::memory_order_relaxed);
            Debug(a_actor, "tree took defend", static_cast<int>(a_kind));
        }
        return taken;
    }

    bool DefendUnavailable(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return false;
        }
        const auto r = g_defendRefusals.Find(a_actor->GetFormID());
        return r && r->streak >= kRefusalStreak &&
               Clock::now() - r->last < std::chrono::duration_cast<Clock::duration>(kRefusalHold);
    }

    void PostDualBlock(RE::Actor *a_actor, float a_seconds)
    {
        if (!a_actor) {
            return;
        }
        const auto until = Clock::now() + std::chrono::duration_cast<Clock::duration>(Seconds{a_seconds});
        g_board.ModifyOrCreate(a_actor->GetFormID(), [&](Entry &e) { e.dualBlockUntil = (std::max)(e.dualBlockUntil, until); });
        Debug(a_actor, "post dual block (ms)", static_cast<int>(a_seconds * 1000.0f));
    }

    float PostRangedShot(RE::Actor *a_actor, float a_factor)
    {
        if (!a_actor) {
            return a_factor;
        }
        // Capped hold: run 48's sorcerer held fire 15 s behind an ally it couldn't step around
        const auto kMaxHold = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<float>(Config::GetInstance().GetDecisionMatrix().shotHoldMaxSeconds));
        constexpr auto kRelease = std::chrono::milliseconds(1500);
        const auto now = Clock::now();
        float out = a_factor;
        g_board.ModifyOrCreate(a_actor->GetFormID(), [&](Entry &e) {
            if (a_factor > 0.0f) {
                e.holdSince = {};
            } else {
                if (e.holdSince == Clock::time_point{} || now >= e.shotUntil || now - e.holdSince >= kMaxHold + kRelease) {
                    e.holdSince = now; // a new hold (the last post lapsed, or the release window is over)
                }
                if (now - e.holdSince >= kMaxHold) {
                    out = 1.0f;
                }
            }
            e.shotFactor = out;
            // 1 s: run 49's archer drew in a 0.62 s gap between brain ticks, after a 0.4 s factor had lapsed
            e.shotUntil = now + std::chrono::seconds(1);
        });
        return out;
    }

    float RangedShotFactor(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return 1.0f;
        }
        const auto e = g_board.Find(a_actor->GetFormID());
        return e && Clock::now() < e->shotUntil ? e->shotFactor : 1.0f;
    }

    void EndDualBlockHold(RE::Actor *a_actor)
    {
        if (a_actor) {
            const auto now = Clock::now();
            g_board.Modify(a_actor->GetFormID(), [&](Entry &e) { e.dualBlockUntil = (std::min)(e.dualBlockUntil, now); });
        }
    }

    bool WantsDualBlock(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return false;
        }
        const auto e = g_board.Find(a_actor->GetFormID());
        return e && Clock::now() < e->dualBlockUntil;
    }

    bool StartDualBlock(RE::Actor *a_actor)
    {
        bool start = false;
        if (a_actor) {
            const auto now = Clock::now();
            g_board.Modify(a_actor->GetFormID(), [&](Entry &e) {
                if (!e.dualBlockStarted && now < e.dualBlockUntil) {
                    e.dualBlockStarted = start = true;
                }
            });
        }
        return start;
    }

    bool EndDualBlock(RE::Actor *a_actor, bool a_force)
    {
        bool end = false;
        if (a_actor) {
            const auto now = Clock::now();
            g_board.Modify(a_actor->GetFormID(), [&](Entry &e) {
                if (e.dualBlockStarted && (a_force || now >= e.dualBlockUntil)) {
                    e.dualBlockStarted = false;
                    e.dualBlockUntil = {};
                    end = true;
                }
            });
        }
        return end;
    }

    bool DualBlockStarted(RE::FormID a_formID)
    {
        const auto e = g_board.Find(a_formID);
        return e && e->dualBlockStarted;
    }

    bool DualBlockOverdue(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return false;
        }
        const auto e = g_board.Find(a_actor->GetFormID());
        return e && e->dualBlockStarted && Clock::now() >= e->dualBlockUntil + std::chrono::milliseconds(300);
    }

    bool PostMove(RE::Actor *a_actor, Move a_kind, float a_param)
    {
        if (!a_actor) {
            return false;
        }
        const auto until = Clock::now() + std::chrono::duration_cast<Clock::duration>(kMoveTTL);
        bool changed = false;
        g_board.ModifyOrCreate(a_actor->GetFormID(), [&](Entry &e) {
            changed = e.move != a_kind;
            e.move = a_kind;
            e.moveUntil = until;
            e.param = a_kind == Move::Circle || a_kind == Move::Advance ? a_param : 0.0f;
        });
        if (changed) {
            g_count.moveChanged[static_cast<int>(a_kind)].fetch_add(1, std::memory_order_relaxed);
            Debug(a_actor, "post move", static_cast<int>(a_kind));
        }
        return true;
    }

    void ClearMove(RE::Actor *a_actor)
    {
        if (a_actor) {
            g_board.Modify(a_actor->GetFormID(), [](Entry &e) { e.move = Move::None; });
        }
    }

    bool WantsMove(RE::Actor *a_actor, Move a_kind)
    {
        if (!a_actor) {
            return false;
        }
        const auto e = g_board.Find(a_actor->GetFormID());
        const bool wants = e && e->move == a_kind && Clock::now() < e->moveUntil;
        if (wants && Config::GetInstance().IsDebugTarget(a_actor->GetFormID()) &&
            g_moveRun.GetOrDefault(a_actor->GetFormID(), Move::None) != a_kind) {
            g_moveRun.ModifyOrCreate(a_actor->GetFormID(), [&](Move &m) { m = a_kind; });
            Debug(a_actor, "tree runs move", static_cast<int>(a_kind));
        }
        return wants;
    }

    Move WantedMove(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return Move::None;
        }
        const auto e = g_board.Find(a_actor->GetFormID());
        return e && Clock::now() < e->moveUntil ? e->move : Move::None;
    }

    void NotePreempted(bool a_vanillaNode)
    {
        g_count.preempted[a_vanillaNode ? 1 : 0].fetch_add(1, std::memory_order_relaxed);
    }

    float CircleSide(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return 0.0f;
        }
        const auto e = g_board.Find(a_actor->GetFormID());
        return e && e->move == Move::Circle && Clock::now() < e->moveUntil ? e->param : 0.0f;
    }

    void NoteCircleRefused(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return;
        }
        const auto now = Clock::now();
        g_board.Modify(a_actor->GetFormID(), [&](Entry &e) {
            if (e.move == Move::Circle && e.param != 0.0f) {
                e.refusedSide = e.param;
                e.refusedAt = now;
            }
        });
    }

    void PostAttackVeto(RE::Actor *a_actor, float a_seconds)
    {
        if (!a_actor) {
            return;
        }
        const auto until = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(a_seconds));
        g_board.ModifyOrCreate(a_actor->GetFormID(), [&](Entry &e) { e.attackVetoUntil = until; });
    }

    void NoteSwingStarted(RE::Actor *a_actor)
    {
        if (a_actor) {
            const auto now = Clock::now();
            g_board.ModifyOrCreate(a_actor->GetFormID(), [&](Entry &e) { e.swingAt = now; });
        }
    }

    float SinceSwing(RE::Actor *a_actor)
    {
        const auto e = a_actor ? g_board.Find(a_actor->GetFormID()) : std::nullopt;
        if (!e || e->swingAt == Clock::time_point{}) {
            return 999.0f;
        }
        return std::chrono::duration<float>(Clock::now() - e->swingAt).count();
    }

    bool AttackVetoed(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return false;
        }
        const auto e = g_board.Find(a_actor->GetFormID());
        return e && Clock::now() < e->attackVetoUntil;
    }

    bool CircleRefused(RE::Actor *a_actor, float a_side)
    {
        if (!a_actor || a_side == 0.0f) {
            return false;
        }
        const auto e = g_board.Find(a_actor->GetFormID());
        return e && e->refusedSide == a_side && Clock::now() - e->refusedAt < std::chrono::seconds(2);
    }

    float AdvanceDistance(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return 0.0f;
        }
        const auto e = g_board.Find(a_actor->GetFormID());
        return e && e->move == Move::Advance && Clock::now() < e->moveUntil ? e->param : 0.0f;
    }

    bool HasPending(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return false;
        }
        const auto e = g_board.Find(a_actor->GetFormID());
        const auto now = Clock::now();
        return e && ((e->attack != Attack::None && now < e->attackUntil) ||
                     (e->defend != Defend::None && now < e->defendUntil) || now < e->dualBlockUntil);
    }

    Attack CommittedAttack(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return Attack::None;
        }
        const auto e = g_board.Find(a_actor->GetFormID());
        return e && Clock::now() < e->committedUntil ? e->committed : Attack::None;
    }

    void NoteAttackMatched(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return;
        }
        bool first = false;
        g_matched.Modify(a_actor->GetFormID(), [&](bool &m) {
            first = !m;
            m = true;
        });
        if (first) {
            const auto kind = CommittedAttack(a_actor);
            if (kind != Attack::None) {
                g_count.attackMatched[static_cast<int>(kind)].fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    void LogSummary(float a_deltaTime)
    {
        static float elapsed = 0.0f; // main thread only
        elapsed += a_deltaTime;
        if (elapsed < 60.0f) {
            return;
        }
        elapsed = 0.0f;
        auto take = [](std::atomic<std::uint32_t> &a) { return a.exchange(0, std::memory_order_relaxed); };
        const std::uint32_t lp = take(g_count.attackPosted[1]), lt = take(g_count.attackTaken[1]), lm = take(g_count.attackMatched[1]);
        const std::uint32_t pp = take(g_count.attackPosted[2]), pt = take(g_count.attackTaken[2]), pm = take(g_count.attackMatched[2]);
        const std::uint32_t bp = take(g_count.defendPosted[1]), bt = take(g_count.defendTaken[1]);
        const std::uint32_t sp = take(g_count.defendPosted[2]), st = take(g_count.defendTaken[2]);
        const std::uint32_t ma = take(g_count.moveChanged[1]), mc = take(g_count.moveChanged[2]), mf = take(g_count.moveChanged[3]), mb = take(g_count.moveChanged[4]);
        const std::uint32_t po = take(g_count.preempted[0]), pv = take(g_count.preempted[1]);
        if (lp + pp + bp + sp + ma + mc + mf + mb + po + pv == 0) {
            return;
        }
        // Which actor's block/bash requests the tree never took (expired and re-posted)
        std::uint32_t expired = 0, worst = 0;
        RE::FormID worstID = 0;
        g_defendExpired.WithWriteLock([&](auto &map) {
            for (const auto &[id, n] : map) {
                expired += n;
                if (n > worst) {
                    worst = n;
                    worstID = id;
                }
            }
            map.clear();
        });
        LOG_INFO("Intents (last 60s, posted/taken/attack data found): light {}/{}/{}, power {}/{}/{}, block {}/{}, bash {}/{}; "
                 "movement changes: advance {}, circle {}, fallback {}, backoff {}; running moves stopped: ours {}, vanilla {}; "
                 "block/bash never taken: {} (most 0x{:08X} x{})",
                 lp, lt, lm, pp, pt, pm, bp, bt, sp, st, ma, mc, mf, mb, po, pv, expired, worstID, worst);
    }

    void Evict(RE::FormID a_formID)
    {
        g_matched.Erase(a_formID);
        g_board.Erase(a_formID);
        g_moveRun.Erase(a_formID);
        g_defendExpired.Erase(a_formID);
        g_defendRefusals.Erase(a_formID);
    }
} // namespace CombatAI::Intents
