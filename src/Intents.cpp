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
            float circleSide = 0.0f;
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
            }
        });
        if (taken) {
            g_count.defendTaken[static_cast<int>(a_kind)].fetch_add(1, std::memory_order_relaxed);
            Debug(a_actor, "tree took defend", static_cast<int>(a_kind));
        }
        return taken;
    }

    bool PostMove(RE::Actor *a_actor, Move a_kind, float a_circleSide)
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
            e.circleSide = a_kind == Move::Circle ? a_circleSide : 0.0f;
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
        return e && e->move == Move::Circle && Clock::now() < e->moveUntil ? e->circleSide : 0.0f;
    }

    bool HasPending(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return false;
        }
        const auto e = g_board.Find(a_actor->GetFormID());
        const auto now = Clock::now();
        return e && ((e->attack != Attack::None && now < e->attackUntil) ||
                     (e->defend != Defend::None && now < e->defendUntil));
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
        LOG_INFO("Intents (last 60s, posted/taken/attack data found): light {}/{}/{}, power {}/{}/{}, block {}/{}, bash {}/{}; "
                 "movement changes: advance {}, circle {}, fallback {}, backoff {}; running moves stopped: ours {}, vanilla {}",
                 lp, lt, lm, pp, pt, pm, bp, bt, sp, st, ma, mc, mf, mb, po, pv);
    }

    void Evict(RE::FormID a_formID)
    {
        g_matched.Erase(a_formID);
        g_board.Erase(a_formID);
        g_moveRun.Erase(a_formID);
    }
} // namespace CombatAI::Intents
