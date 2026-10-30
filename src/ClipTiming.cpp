#include "ClipTiming.h"
#include "ActorUtils.h"
#include "Config.h"
#include "Logger.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CombatAI::ClipTiming
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        float Since(Clock::time_point a_from, Clock::time_point a_to)
        {
            return std::chrono::duration<float>(a_to - a_from).count();
        }

        // What an attack clip's annotations say about it (clip seconds; -1 = no mark). MCO and BFCO movesets carry
        // all of it (1.7.7 run, player BFCO_Attack1-5): HitFrame, MCO_WinOpen/WinClose, MCO_Recovery,
        // PIE.@SGVI|MCO_nextattack|N and MCO_nextpowerattack|N (BFCO: BFCO_NextIsAttackN,
        // BFCO_NextIsPowerAttackN), and AMR "animmotion x y z" keys: the root's position
        // (absolute, rising 0 -> 28 -> 36 -> 70 over BFCO_Attack2), y forward, x to the side
        struct Profile
        {
            float startup = -1.0f, recovery = -1.0f, winOpen = -1.0f, winClose = -1.0f, speed = -1.0f;
            float forward = 0.0f, side = 0.0f, forwardEnd = 0.0f; // root motion by the first blow, and at the last key
            bool hasMotion = false;
            int nextLight = -1, nextPower = -1, rotations = 0;
        };

        // The number right after a_key in a_text (e.g. "PIE.@SGVI|MCO_nextattack|2"), or -1
        float AfterKey(std::string_view a_text, std::string_view a_key)
        {
            const auto at = a_text.find(a_key);
            return at == std::string_view::npos ? -1.0f : std::strtof(a_text.data() + at + a_key.size(), nullptr);
        }

        // What one read of the attacker's graph found
        struct Clip
        {
            const void *animation = nullptr; // identity of the playing clip
            std::string name;
            std::vector<float> hits; // HitFrame times, clip seconds
            float duration = 0.0f;
            float localTime = 0.0f;
            float playbackSpeed = 1.0f;
            int graph = -1; // index in the graph manager
            // Precision's hit window (its PrecisionHandler animation event handler): the weapon collision opens at
            // Collision_AttackStart/Collision_Start when the clip has Precision marks, else at weaponSwing/
            // weaponLeftSwing (preHitFrame for some vanilla attacks), and closes at Collision_AttackEnd/attackStop/
            // Collision_Cancel. A blow lands on contact anywhere inside it, so it can come before HitFrame
            float open = -1.0f, close = -1.0f; // clip seconds, -1 = no mark
            const char *openName = "";
            const char *closeName = "";
            std::string marks; // every annotation "text@time", for the debug log
            Profile profile;   // read only when asked (LogOwnAttack)
        };

        // An annotation is the event name, optionally followed by ".payload"
        bool IsEvent(const char *a_text, std::string_view a_name)
        {
            return _strnicmp(a_text, a_name.data(), a_name.size()) == 0 &&
                   (a_text[a_name.size()] == '\0' || a_text[a_name.size()] == '.');
        }

        // Fills the hit window and marks of a clip from its annotations
        void ReadWindow(const RE::hkaAnimation &a_animation, Clip &a_clip)
        {
            float precisionOpen = -1.0f, swingOpen = -1.0f, preHitOpen = -1.0f;
            std::vector<std::pair<float, const char *>> closes;
            int listed = 0;
            for (auto &annotation : a_animation.annotationTracks[0].annotations) {
                const char *text = annotation.text.c_str();
                if (!text) {
                    continue;
                }
                const float t = annotation.time;
                if (listed++ < 24) {
                    a_clip.marks += std::format("{}{}@{:.2f}", a_clip.marks.empty() ? "" : " ", text, t);
                }
                if (IsEvent(text, "Collision_AttackStart") || IsEvent(text, "Collision_Start")) {
                    if (precisionOpen < 0.0f || t < precisionOpen) {
                        precisionOpen = t;
                    }
                } else if (IsEvent(text, "weaponSwing") || IsEvent(text, "weaponLeftSwing")) {
                    if (swingOpen < 0.0f || t < swingOpen) {
                        swingOpen = t;
                    }
                } else if (IsEvent(text, "preHitFrame")) {
                    if (preHitOpen < 0.0f || t < preHitOpen) {
                        preHitOpen = t;
                    }
                } else if (IsEvent(text, "Collision_AttackEnd")) {
                    closes.emplace_back(t, "Collision_AttackEnd");
                } else if (IsEvent(text, "attackStop")) {
                    closes.emplace_back(t, "attackStop");
                } else if (IsEvent(text, "Collision_Cancel")) {
                    closes.emplace_back(t, "Collision_Cancel");
                }
            }
            if (precisionOpen >= 0.0f) {
                a_clip.open = precisionOpen;
                a_clip.openName = "Collision_AttackStart";
            } else if (swingOpen >= 0.0f) {
                // Precision's default; preHitFrame only for attacks its config names (1.6.2 run: BFCO_Attack1
                // preHitFrame 0.40, WeaponSwing 0.57, HitFrame 0.68, every blow at 0.58-0.71)
                a_clip.open = swingOpen;
                a_clip.openName = "weaponSwing";
            } else if (preHitOpen >= 0.0f) {
                a_clip.open = preHitOpen;
                a_clip.openName = "preHitFrame";
            }
            for (const auto &[t, name] : closes) { // the first close after the open
                if (t >= a_clip.open && (a_clip.close < 0.0f || t < a_clip.close)) {
                    a_clip.close = t;
                    a_clip.closeName = name;
                }
            }
        }

        Profile ReadProfile(const RE::hkaAnimation &a_animation, const std::vector<float> &a_hits)
        {
            Profile p;
            p.startup = a_hits.empty() ? -1.0f : a_hits.front();
            const auto earliest = [](float &a_slot, float a_t) {
                if (a_slot < 0.0f || a_t < a_slot) {
                    a_slot = a_t;
                }
            };
            std::vector<std::array<float, 3>> motion; // time, x, y
            for (auto &annotation : a_animation.annotationTracks[0].annotations) {
                const char *text = annotation.text.c_str();
                if (!text) {
                    continue;
                }
                const float t = annotation.time;
                const std::string_view view(text);
                float x = 0.0f, y = 0.0f, z = 0.0f;
                if (IsEvent(text, "MCO_Recovery") || IsEvent(text, "BFCO_DIY_recovery")) {
                    earliest(p.recovery, t);
                } else if (IsEvent(text, "MCO_WinOpen") || IsEvent(text, "BFCO_NextWinStart")) {
                    earliest(p.winOpen, t);
                } else if (IsEvent(text, "MCO_WinClose") || IsEvent(text, "BFCO_DIY_EndLoop")) {
                    earliest(p.winClose, t);
                } else if (_strnicmp(text, "animmotion ", 11) == 0 && sscanf_s(text + 11, "%f %f %f", &x, &y, &z) == 3) {
                    motion.push_back({t, x, y});
                } else if (_strnicmp(text, "animrotation", 12) == 0) {
                    ++p.rotations;
                } else if (const float n = AfterKey(view, "MCO_nextattack|"); n >= 0.0f) {
                    p.nextLight = static_cast<int>(n);
                } else if (const float n2 = AfterKey(view, "MCO_nextpowerattack|"); n2 >= 0.0f) {
                    p.nextPower = static_cast<int>(n2);
                } else if (const float b = AfterKey(view, "BFCO_NextIsAttack"); b >= 0.0f) {
                    p.nextLight = static_cast<int>(b);
                } else if (const float b2 = AfterKey(view, "BFCO_NextIsPowerAttack"); b2 >= 0.0f) {
                    p.nextPower = static_cast<int>(b2);
                } else if (const float s = AfterKey(view, "AttackSpeed|"); s > 0.0f) {
                    p.speed = s;
                }
            }
            if (!motion.empty()) {
                std::ranges::sort(motion, {}, [](const auto &a_key) { return a_key[0]; });
                // Root position at a clip time, linear between keys
                const auto at = [&](float a_t, int a_axis) {
                    if (a_t <= motion.front()[0]) {
                        return motion.front()[a_axis];
                    }
                    for (std::size_t i = 1; i < motion.size(); ++i) {
                        if (a_t <= motion[i][0]) {
                            const float span = motion[i][0] - motion[i - 1][0];
                            const float f = span > 0.0f ? (a_t - motion[i - 1][0]) / span : 1.0f;
                            return motion[i - 1][a_axis] + f * (motion[i][a_axis] - motion[i - 1][a_axis]);
                        }
                    }
                    return motion.back()[a_axis];
                };
                p.hasMotion = true;
                const float hit = p.startup >= 0.0f ? p.startup : motion.back()[0];
                p.forward = at(hit, 2) - motion.front()[2];
                p.side = at(hit, 1) - motion.front()[1];
                p.forwardEnd = motion.back()[2] - motion.front()[2];
            }
            return p;
        }

        enum class ReadResult
        {
            kFound,
            kNoGraph,
            kNoHitFrame
        };

        // The attack clip (an active clip with a HitFrame annotation) playing now; every graph of the manager is
        // searched, the player's first-person graph included if the manager holds it. During a transition both clips
        // are active: the newest one (lowest local time) with a HitFrame still ahead wins. 1.6.0 took the first one
        // listed and stayed on the light clip a power attack started from (6 power blows read 0.4-0.7 s late)
        ReadResult Read(RE::Actor *a_actor, Clip &a_out, bool a_profile = false)
        {
            RE::BSAnimationGraphManagerPtr manager;
            if (!a_actor || !a_actor->GetAnimationGraphManager(manager) || !manager || manager->graphs.empty()) {
                return ReadResult::kNoGraph;
            }
            RE::BSSpinLockGuard lock(manager->GetRuntimeData().updateLock);
            bool found = false, foundAhead = false;
            for (std::uint32_t g = 0; g < manager->graphs.size(); ++g) {
                auto *graph = manager->graphs[g] ? manager->graphs[g]->behaviorGraph : nullptr;
                auto *activeNodes = graph ? graph->activeNodes : nullptr;
                if (!activeNodes) {
                    continue;
                }
                for (auto &info : *activeNodes) {
                    auto *node = info.nodeClone;
                    const auto *type = node ? node->GetClassType() : nullptr;
                    if (!type || !type->name || _stricmp(type->name, "hkbClipGenerator") != 0) {
                        continue;
                    }
                    auto *clip = static_cast<RE::hkbClipGenerator *>(node);
                    auto *animation = clip->binding ? clip->binding->animation.get() : nullptr;
                    if (!animation || animation->annotationTracks.empty()) {
                        continue;
                    }
                    std::vector<float> hits;
                    for (auto &annotation : animation->annotationTracks[0].annotations) {
                        const char *text = annotation.text.c_str();
                        if (text && _stricmp(text, "HitFrame") == 0) {
                            hits.push_back(annotation.time);
                        }
                    }
                    if (hits.empty()) {
                        continue;
                    }
                    std::ranges::sort(hits);
                    const bool ahead = hits.back() > clip->localTime;
                    if (found && (foundAhead > ahead || (foundAhead == ahead && clip->localTime >= a_out.localTime))) {
                        continue; // the one kept is newer, or still has its blow ahead
                    }
                    const char *name = clip->animationName.c_str();
                    a_out = {animation, name ? name : "", std::move(hits), animation->duration, clip->localTime,
                             clip->playbackSpeed, static_cast<int>(g)};
                    ReadWindow(*animation, a_out);
                    if (a_profile) {
                        a_out.profile = ReadProfile(*animation, a_out.hits); // under the lock: the clip may unload after
                    }
                    found = true;
                    foundAhead = ahead;
                }
            }
            return found ? ReadResult::kFound : ReadResult::kNoHitFrame;
        }

        // Below this rate the clip is held, not swinging: BFCO starts a light attack on a power clip at playback speed
        // 0.2 until it knows light from power (1.7.0 run), so the blow's timing isn't known yet
        constexpr float kMinRate = 0.5f;

        // Per attacker: the clip seen last, and the predictions made for its next blow
        struct Track
        {
            Clip clip;
            Clock::time_point at{};
            Clock::time_point since{}; // when this clip was first read
            // Clip seconds per second: playbackSpeed x ratio, known from the first read of a clip. 1.7.0 measured the
            // rate between two reads, too late for most commits (47 of 48 came at the swing's first tick)
            float rate = 0.0f;
            float ratio = 1.0f; // measured rate / playbackSpeed, learned (1.6.0 run: 1.10-1.13 at speed 1.30)
            // Earliest prediction for the next blow at the current playback speed, and the brain's at that same tick
            bool predicted = false;
            Clock::time_point clipAt{}, medianAt{};
            bool hasMedian = false;
            float horizon = 0.0f; // seconds before the predicted blow the prediction was made
            int hitIndex = -1;    // which HitFrame it was for
            int consumed = -1;    // the last HitFrame of this clip a blow was scored for (not predicted again)
            bool debug = false;
            std::uint32_t swing = 0; // a selected NPC's count of this attacker's swings, for the log's swing tag
            // The last blow counted: a second hit event on the same victim right after is the same blow
            RE::FormID lastVictim = 0;
            Clock::time_point lastBlowAt{};
        };

        std::mutex g_lock;
        std::unordered_map<RE::FormID, Track> g_tracks;

        // Each NPC's attack clip read last (ObserveOwnAttack, main thread)
        struct OwnLast
        {
            const void *clip = nullptr;
            float time = 0.0f;
        };
        std::unordered_map<RE::FormID, OwnLast> g_ownLast;
        std::unordered_set<const void *> g_marksLogged; // clips whose annotations were listed once

        // Each NPC's attacks seen so far (ObserveOwnAttack): seconds to the first blow at the clip's playback speed,
        // and the root motion by then. Main thread
        struct OwnAttack
        {
            float startup = 0.0f, forward = 0.0f;
        };
        std::unordered_map<RE::FormID, std::unordered_map<std::string, OwnAttack>> g_ownAttacks;
        std::pair<RE::FormID, std::uint32_t> g_punishLogged{}; // the swing LogPunishWindow logged last

        struct Stats
        {
            int blows = 0, compared = 0, noPrediction = 0, noClip = 0, stale = 0;
            double clipSum = 0.0, clipSumSq = 0.0, clipAbs = 0.0;
            double medianSum = 0.0, medianSumSq = 0.0, medianAbs = 0.0;
            int medianCount = 0;
            double horizonSum = 0.0;
            // Where scored blows fell in their clip, and which hit window marks the clip had
            int beforeHitFrame = 0, precisionWindow = 0, vanillaWindow = 0, noWindow = 0, extraContacts = 0;
            double earlySum = 0.0; // clip seconds before HitFrame, blows that came early
        } g_stats;

        std::string Window(const Clip &a_clip)
        {
            return std::format("window {} {}, {} {}",
                               a_clip.open >= 0.0f ? std::format("{:.2f}", a_clip.open) : std::string("none"),
                               a_clip.openName,
                               a_clip.close >= 0.0f ? std::format("{:.2f}", a_clip.close) : std::string("none"),
                               a_clip.closeName);
        }

        std::string Brief(const std::vector<float> &a_hits)
        {
            std::string out;
            for (const float h : a_hits) {
                out += std::format("{}{:.2f}", out.empty() ? "" : ",", h);
            }
            return out;
        }
    } // namespace

    const char *Name(Eta a_eta)
    {
        switch (a_eta) {
        case Eta::kOff:
            return "off";
        case Eta::kNoClip:
            return "noclip";
        case Eta::kHeld:
            return "held";
        case Eta::kStale:
            return "stale";
        case Eta::kBlow:
            return "blow";
        default:
            return "past";
        }
    }

    void Sample(RE::Actor *a_attacker, float a_medianTimeToHit, bool a_debug, std::uint32_t a_swing)
    {
        if (!a_attacker) {
            return;
        }
        const auto now = Clock::now();
        const RE::FormID id = a_attacker->GetFormID();
        {
            std::scoped_lock lock(g_lock);
            if (const auto it = g_tracks.find(id); it != g_tracks.end() && Since(it->second.at, now) < 0.03f) {
                it->second.debug |= a_debug;
                if (a_debug) {
                    it->second.swing = a_swing;
                }
                return; // another NPC sampled this attacker this frame
            }
        }
        Clip clip;
        const auto result = Read(a_attacker, clip, true); // profiled: recovery and combo window for LogPunishWindow

        std::scoped_lock lock(g_lock);
        auto &track = g_tracks[id];
        if (a_debug) {
            track.swing = a_swing;
        }
        if (result != ReadResult::kFound) {
            if (a_debug && track.clip.animation) {
                LOG_DEBUG("ClipTiming 0x{:08X}: swing with no attack clip found ({}) {}", id,
                          result == ReadResult::kNoGraph ? "no graph" : "no active clip with a HitFrame",
                          SwingTag(id, a_swing));
            }
            track.clip = {};
            track.at = now;
            return;
        }
        const bool sameClip = clip.animation == track.clip.animation && clip.localTime >= track.clip.localTime;
        if (!sameClip) {
            // A new swing (or the same clip restarted): predictions start over
            track.since = now;
            track.predicted = false;
            track.consumed = -1;
            track.debug = a_debug;
            if (a_debug) {
                bool firstPerson = false;
                if (a_attacker->IsPlayerRef()) {
                    const auto *camera = RE::PlayerCamera::GetSingleton();
                    firstPerson = camera && camera->IsInFirstPerson();
                }
                LOG_DEBUG("ClipTiming 0x{:08X}: clip '{}' ({:.2f}s, HitFrame {}, {}, playbackSpeed {:.2f}, graph {}{}) "
                          "at {:.2f}s {}; marks: {}",
                          id, clip.name, clip.duration, Brief(clip.hits), Window(clip), clip.playbackSpeed, clip.graph,
                          firstPerson ? ", first person" : "", clip.localTime, SwingTag(id, a_swing), clip.marks);
            }
        } else {
            const float dt = Since(track.at, now);
            const bool speedChanged = std::abs(clip.playbackSpeed - track.clip.playbackSpeed) > 0.01f;
            if (!speedChanged && dt > 0.05f && clip.localTime > track.clip.localTime && clip.playbackSpeed > 0.05f) {
                const float measured = (clip.localTime - track.clip.localTime) / dt;
                track.ratio += 0.3f * (std::clamp(measured / clip.playbackSpeed, 0.5f, 1.5f) - track.ratio);
            }
            if (speedChanged) {
                track.predicted = false; // a held clip released (or sped up): its blow is predicted afresh
            }
            track.debug |= a_debug;
        }
        track.clip = std::move(clip);
        track.at = now;
        track.rate = track.clip.playbackSpeed * track.ratio;

        // The next HitFrame ahead of the clip's position, while the clip plays at swing speed
        if (track.rate < kMinRate) {
            return;
        }
        int next = -1;
        // A blow can land before its HitFrame (Precision contact): that HitFrame is done, not predicted again
        // (1.6.1 run: one power swing scored twice, its third contact called a missed swing)
        for (int i = track.consumed + 1; i < static_cast<int>(track.clip.hits.size()); ++i) {
            if (track.clip.hits[i] > track.clip.localTime) {
                next = i;
                break;
            }
        }
        if (next < 0 || (track.predicted && track.hitIndex == next)) {
            return; // past the last blow, or this blow is already predicted (the earliest prediction is the test)
        }
        const float ahead = (track.clip.hits[next] - track.clip.localTime) / track.rate;
        track.predicted = true;
        track.hitIndex = next;
        track.horizon = ahead;
        track.clipAt = now + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(ahead));
        track.hasMedian = a_medianTimeToHit < 900.0f;
        if (track.hasMedian) {
            track.medianAt =
                now + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(a_medianTimeToHit));
        }
        if (a_debug) {
            LOG_DEBUG("ClipTiming 0x{:08X}: at {:.2f}s, rate {:.2f} (speed x ratio {:.2f}) -> HitFrame {:.2f} in {:.2f}s "
                      "(median rule: {}) {}",
                      id, track.clip.localTime, track.rate, track.ratio, track.clip.hits[next], ahead,
                      track.hasMedian ? std::format("{:.2f}s", a_medianTimeToHit) : std::string("none"),
                      SwingTag(id, a_swing));
        }
    }

    Eta Predict(RE::Actor *a_attacker, float a_swingElapsed, float &a_earliest, float &a_latest)
    {
        // The hit event comes this long after the clip reaches HitFrame (1.6.x runs, clip error median +0.02 to
        // +0.04 s)
        constexpr float kHitLag = 0.03f;
        if (!a_attacker) {
            return Eta::kNoClip;
        }
        const auto now = Clock::now();
        std::scoped_lock lock(g_lock);
        const auto it = g_tracks.find(a_attacker->GetFormID());
        if (it == g_tracks.end()) {
            return Eta::kNoClip;
        }
        const auto &track = it->second;
        if (!track.clip.animation || Since(track.at, now) > 0.15f) {
            return Eta::kNoClip;
        }
        if (track.rate < kMinRate) {
            return Eta::kHeld;
        }
        const float position = track.clip.localTime + track.rate * Since(track.at, now);
        for (int i = track.consumed + 1; i < static_cast<int>(track.clip.hits.size()); ++i) {
            const float hit = track.clip.hits[i];
            if (hit + kHitLag * track.rate < position) {
                continue; // passed (missed, or its hit event is late)
            }
            // Contact opens at the window mark for the clip's first blow, at the HitFrame for later ones
            const float open = i == 0 && track.clip.open >= 0.0f && track.clip.open < hit ? track.clip.open : hit;
            a_latest = (std::max)(0.0f, (hit - position) / track.rate + kHitLag);
            a_earliest = (std::max)(0.0f, (std::min)((open - position) / track.rate, a_latest));
            return Eta::kBlow;
        }
        // Every blow of the clip is past. A clip read since before this swing started is the last swing's, still
        // playing out while the new one's isn't read yet (1.7.3 run: 97 of 174 swings began with 1-6 such ticks):
        // nothing known, the median rule stands. 0.15 s: another NPC may have seen the swing (and read its clip) a
        // tick earlier. An attacker still winding up has its blow ahead: a past clip none of whose blows landed this
        // swing is the last swing's (1.7.7 run: BFCO combos read the old clip up to 0.33 s into the next wind-up,
        // started under 0.15 s after it)
        const bool windingUp = ActorUtils::SafeGetAttackState(a_attacker) == RE::ATTACK_STATE_ENUM::kDraw &&
                               Since(track.lastBlowAt, now) > a_swingElapsed;
        if (a_swingElapsed >= 0.0f && (windingUp || Since(track.since, now) > a_swingElapsed + 0.15f)) {
            return Eta::kStale;
        }
        // The clip's blows are all past: none is coming from it. The median rule would still see one (1.7.1 run: 3
        // late defend posts right after the clip's HitFrame passed)
        a_earliest = a_latest = 999.0f;
        return Eta::kPast;
    }

    void OnHit(RE::Actor *a_attacker, RE::Actor *a_victim, bool a_power, std::uint32_t a_swing)
    {
        if (!a_attacker || !a_victim) {
            return;
        }
        const auto now = Clock::now();
        std::scoped_lock lock(g_lock);
        const auto it = g_tracks.find(a_attacker->GetFormID());
        if (it == g_tracks.end() || Since(it->second.at, now) > 1.5f) {
            return; // no brain NPC watched this swing
        }
        auto &track = it->second;
        // 1.6.0 run: two hit events 1-24 ms apart for one blow; the second was scored against the next HitFrame
        if (track.lastVictim == a_victim->GetFormID() && Since(track.lastBlowAt, now) < 0.1f) {
            return;
        }
        // The clip's position at this blow, from the last sample at the measured rate
        const float blowTime = track.clip.localTime + track.rate * Since(track.at, now);
        // Another contact of a swing already scored (a sweep reaching a second victim, or the same one again inside
        // the hit window): the same blow, not a new one
        if (!track.predicted && track.consumed >= 0 && track.clip.animation && track.rate > 0.0f &&
            blowTime <= (track.clip.close >= 0.0f ? track.clip.close : track.clip.hits.back() + 0.3f)) {
            ++g_stats.extraContacts;
            return;
        }
        track.lastVictim = a_victim->GetFormID();
        track.lastBlowAt = now;
        ++g_stats.blows;
        if (!track.clip.animation) {
            ++g_stats.noClip;
            return;
        }
        if (!track.predicted) {
            ++g_stats.noPrediction; // no prediction for it: its clip was still held (BFCO deciding light or power)
            return;
        }
        const float clipError = Since(track.clipAt, now); // + = the blow came later than predicted
        const bool debug = track.debug || Config::GetInstance().IsDebugTarget(a_victim->GetFormID());
        if (clipError > 0.3f) {
            // The predicted blow never landed (a miss) and this one came from a swing whose clip was not read
            // (1.6.0 run: a missed special attack scored +0.78 s); the real lag after HitFrame is under ~0.15 s
            ++g_stats.stale;
            if (debug) {
                LOG_DEBUG("ClipTiming 0x{:08X} -> 0x{:08X}: {} blow {:.2f}s after the predicted one (clip '{}' HitFrame "
                          "{:.2f}): that swing missed, not scored. kind={} clip={} hf={:.2f} late={:.2f} result=missed "
                          "{}",
                          a_attacker->GetFormID(), a_victim->GetFormID(), a_power ? "power" : "light", clipError,
                          track.clip.name, track.clip.hits[track.hitIndex], a_power ? "power" : "light",
                          track.clip.name, track.clip.hits[track.hitIndex], clipError,
                          SwingTag(a_attacker->GetFormID(), a_swing));
            }
            track.predicted = false;
            return;
        }
        ++g_stats.compared;
        const float hitFrame = track.clip.hits[track.hitIndex];
        if (blowTime < hitFrame) {
            ++g_stats.beforeHitFrame;
            g_stats.earlySum += hitFrame - blowTime;
        }
        ++(track.clip.open < 0.0f                                     ? g_stats.noWindow
           : _stricmp(track.clip.openName, "Collision_AttackStart") == 0 ? g_stats.precisionWindow
                                                                        : g_stats.vanillaWindow);
        g_stats.clipSum += clipError;
        g_stats.clipSumSq += static_cast<double>(clipError) * clipError;
        g_stats.clipAbs += std::abs(clipError);
        g_stats.horizonSum += track.horizon;
        float medianError = 0.0f;
        if (track.hasMedian) {
            medianError = Since(track.medianAt, now);
            ++g_stats.medianCount;
            g_stats.medianSum += medianError;
            g_stats.medianSumSq += static_cast<double>(medianError) * medianError;
            g_stats.medianAbs += std::abs(medianError);
        }
        if (debug) {
            LOG_DEBUG("ClipTiming 0x{:08X} -> 0x{:08X}: {} blow, clip '{}' HitFrame {:.2f}: clip predicted it {:.2f}s "
                      "ahead, off by {:+.2f}s; median rule {}; blow at clip {:.2f}s ({}). kind={} clip={} hf={:.2f} "
                      "ahead={:.2f} clipErr={:+.2f} medErr={} at={:.2f} open={:.2f} close={:.2f} {}",
                      a_attacker->GetFormID(), a_victim->GetFormID(), a_power ? "power" : "light", track.clip.name,
                      hitFrame, track.horizon, clipError,
                      track.hasMedian ? std::format("off by {:+.2f}s", medianError) : std::string("had none"), blowTime,
                      Window(track.clip), a_power ? "power" : "light", track.clip.name, hitFrame, track.horizon,
                      clipError, track.hasMedian ? std::format("{:+.2f}", medianError) : std::string("na"), blowTime,
                      track.clip.open, track.clip.close, SwingTag(a_attacker->GetFormID(), a_swing));
        }
        track.predicted = false; // a multi-hit clip predicts its next HitFrame afresh
        track.consumed = track.hitIndex;
    }

    void Update(float a_deltaTime)
    {
        static float elapsed = 0.0f; // main thread only
        elapsed += a_deltaTime;
        if (elapsed < 60.0f) {
            return;
        }
        elapsed = 0.0f;
        Stats s;
        {
            std::scoped_lock lock(g_lock);
            s = g_stats;
            g_stats = {};
            // Drop attackers not seen for a minute
            const auto now = Clock::now();
            std::erase_if(g_tracks, [&](const auto &a_entry) { return Since(a_entry.second.at, now) > 60.0f; });
        }
        if (s.blows == 0) {
            return;
        }
        const auto meanSd = [](double a_sum, double a_sumSq, int a_n) {
            const double mean = a_n ? a_sum / a_n : 0.0;
            const double sd = a_n > 1 ? std::sqrt((std::max)(0.0, a_sumSq / a_n - mean * mean)) : 0.0;
            return std::pair{mean, sd};
        };
        const auto [clipMean, clipSd] = meanSd(s.clipSum, s.clipSumSq, s.compared);
        const auto [medianMean, medianSd] = meanSd(s.medianSum, s.medianSumSq, s.medianCount);
        LOG_INFO("Clip timing (last 60s): {} blows watched, {} compared ({:.2f}s ahead on average); clip error {:+.2f}s "
                 "+- {:.2f} (mean abs {:.2f}); median-rule error {:+.2f}s +- {:.2f} (mean abs {:.2f}, {} blows); no clip "
                 "{}, no prediction (clip held) {}, after a missed prediction {}; blows before HitFrame {} (by {:.2f}s of clip on "
                 "average), hit window marks: Precision {}, vanilla {}, none {}; extra contacts {}",
                 s.blows, s.compared, s.compared ? s.horizonSum / s.compared : 0.0, clipMean, clipSd,
                 s.compared ? s.clipAbs / s.compared : 0.0, medianMean, medianSd,
                 s.medianCount ? s.medianAbs / s.medianCount : 0.0, s.medianCount, s.noClip, s.noPrediction, s.stale, s.beforeHitFrame,
                 s.beforeHitFrame ? s.earlySum / s.beforeHitFrame : 0.0, s.precisionWindow, s.vanillaWindow, s.noWindow,
                 s.extraContacts);
    }

    void ObserveOwnAttack(RE::Actor *a_actor, RE::Actor *a_target, float a_distance, float a_reach, float a_targetRadius,
                          bool a_log)
    {
        // A new attack clip shows during the wind-up or the swing; later states only replay the one read
        const auto state = a_actor ? ActorUtils::SafeGetAttackState(a_actor) : RE::ATTACK_STATE_ENUM::kNone;
        if (state != RE::ATTACK_STATE_ENUM::kDraw && state != RE::ATTACK_STATE_ENUM::kSwing) {
            return;
        }
        Clip clip;
        if (Read(a_actor, clip, true) != ReadResult::kFound) {
            return;
        }
        // A new clip, or the same one started again (a combo can come back to Attack1)
        auto &last = g_ownLast[a_actor->GetFormID()];
        const bool fresh = clip.animation != last.clip || clip.localTime < last.time;
        last = {clip.animation, clip.localTime};
        if (!fresh || clip.hits.back() < clip.localTime) {
            return; // read already, or a clip whose blows are past (the last attack's, still blending out)
        }
        const Profile &p = clip.profile;
        const float playback = (std::max)(clip.playbackSpeed, 0.05f);
        if (p.startup >= 0.0f) {
            g_ownAttacks[a_actor->GetFormID()][clip.name] = {p.startup / playback, p.forward};
        }
        if (!a_log) {
            return;
        }
        // The target's speed toward the NPC (+) or away (-), and the distance when the blow comes (1.7.9 run: two
        // attacks started 213 and 289 out, "short", hit: the player walked into them)
        float closing = 0.0f;
        if (a_target) {
            RE::NiPoint3 velocity;
            a_target->GetLinearVelocity(velocity);
            const auto self = a_actor->GetPosition();
            const auto other = a_target->GetPosition();
            const float dx = self.x - other.x, dy = self.y - other.y;
            const float length = std::hypot(dx, dy);
            closing = length > 1.0f ? (velocity.x * dx + velocity.y * dy) / length : 0.0f;
        }
        const float toBlow = p.startup >= 0.0f ? (std::max)(0.0f, (p.startup - clip.localTime) / playback) : 0.0f;
        const float distanceAtBlow = a_distance - closing * toBlow;
        std::string blows;
        for (const float h : clip.hits) {
            blows += std::format("{}{:.2f}", blows.empty() ? "" : "/", h);
        }
        const auto mark = [](float a_t) { return a_t >= 0.0f ? std::format("{:.2f}", a_t) : std::string("-"); };
        const float reachTotal = a_reach + a_targetRadius + p.forward;
        const auto verdict = [&](float a_dist) {
            return !p.hasMotion             ? std::string("no motion keys")
                   : a_dist <= reachTotal ? std::string("reaches")
                                          : std::format("short by {:.0f}", a_dist - reachTotal);
        };
        const std::string reaches = std::format("{}; target moving {:+.0f}/s toward it, blow in {:.2f}s at dist {:.0f}: {}",
                                                verdict(a_distance), closing, toBlow, distanceAtBlow, verdict(distanceAtBlow));
        LOG_DEBUG("AttackClip 0x{:08X}: '{}' {:.2f}s, playback {:.2f}{}, read at {:.2f}s; startup {}, blows {} ({}), "
                  "recovery {}, window {}-{}, next light {} power {}, forward {:.0f} side {:.0f} by the first blow ({:.0f} "
                  "by the end{}){}; dist {:.0f} vs reach {:.0f} + target {:.0f} + forward {:.0f} = {:.0f}: {}",
                  static_cast<std::uint32_t>(a_actor->GetFormID()), clip.name, clip.duration, clip.playbackSpeed,
                  p.speed > 0.0f ? std::format(" (speed mark {:.2f})", p.speed) : std::string(), clip.localTime,
                  mark(p.startup), clip.hits.size(), blows, mark(p.recovery), mark(p.winOpen), mark(p.winClose),
                  p.nextLight, p.nextPower, p.forward, p.side, p.forwardEnd,
                  p.rotations ? std::format(", {} rotation keys", p.rotations) : std::string(),
                  p.hasMotion ? "" : " (no motion keys)", a_distance, a_reach, a_targetRadius, p.forward, reachTotal,
                  reaches);
        if (g_marksLogged.insert(clip.animation).second) {
            LOG_DEBUG("AttackClip 0x{:08X}: '{}' marks: {}", static_cast<std::uint32_t>(a_actor->GetFormID()), clip.name,
                      clip.marks);
        }
    }

    int FastestAttack(RE::FormID a_actor, float a_distance, float a_closing, float a_reach, float a_targetRadius,
                      float &a_startup)
    {
        const auto own = g_ownAttacks.find(a_actor);
        if (own == g_ownAttacks.end() || own->second.empty()) {
            return -1;
        }
        int found = 0;
        for (const auto &[name, attack] : own->second) {
            const float atBlow = a_distance - a_closing * attack.startup;
            if (atBlow <= a_reach + a_targetRadius + attack.forward && (!found || attack.startup < a_startup)) {
                a_startup = attack.startup;
                found = 1;
            }
        }
        return found;
    }

    bool Recovery(RE::Actor *a_attacker, float &a_recoverIn, float &a_windowIn, bool &a_hasWindow)
    {
        if (!a_attacker) {
            return false;
        }
        const auto now = Clock::now();
        std::scoped_lock lock(g_lock);
        const auto it = g_tracks.find(a_attacker->GetFormID());
        if (it == g_tracks.end() || !it->second.clip.animation || it->second.rate < kMinRate ||
            Since(it->second.at, now) > 0.15f) {
            return false;
        }
        const auto &t = it->second;
        const float position = t.clip.localTime + t.rate * Since(t.at, now);
        const auto &p = t.clip.profile;
        a_recoverIn = ((p.recovery >= 0.0f ? p.recovery : t.clip.duration) - position) / t.rate;
        a_hasWindow = p.winOpen >= 0.0f;
        a_windowIn = a_hasWindow ? (p.winOpen - position) / t.rate : 0.0f;
        return true;
    }

    void LogPunishWindow(RE::Actor *a_npc, RE::Actor *a_attacker, float a_distance, float a_reach, float a_targetRadius,
                         std::uint32_t a_swing)
    {
        if (!a_npc || !a_attacker || g_punishLogged == std::pair{a_npc->GetFormID(), a_swing}) {
            return;
        }
        float recoverIn = 0.0f, windowIn = -1.0f;
        bool marked = false;
        std::string clipName;
        {
            std::scoped_lock lock(g_lock);
            const auto it = g_tracks.find(a_attacker->GetFormID());
            if (it == g_tracks.end() || !it->second.clip.animation || it->second.rate < kMinRate) {
                return;
            }
            const auto &t = it->second;
            const float position = t.clip.localTime + t.rate * Since(t.at, Clock::now());
            const auto &p = t.clip.profile;
            marked = p.recovery >= 0.0f;
            recoverIn = ((marked ? p.recovery : t.clip.duration) - position) / t.rate;
            windowIn = p.winOpen >= 0.0f ? (p.winOpen - position) / t.rate : -1.0f;
            clipName = t.clip.name;
        }
        g_punishLogged = {a_npc->GetFormID(), a_swing};
        // Its fastest attack, and its fastest that also reaches from here
        const auto own = g_ownAttacks.find(a_npc->GetFormID());
        const std::pair<const std::string, OwnAttack> *fastest = nullptr, *fastestInReach = nullptr;
        int fit = 0;
        if (own != g_ownAttacks.end()) {
            for (const auto &entry : own->second) {
                const bool inReach = a_distance <= a_reach + a_targetRadius + entry.second.forward;
                if (!fastest || entry.second.startup < fastest->second.startup) {
                    fastest = &entry;
                }
                if (inReach && (!fastestInReach || entry.second.startup < fastestInReach->second.startup)) {
                    fastestInReach = &entry;
                }
                fit += inReach && entry.second.startup < recoverIn ? 1 : 0;
            }
        }
        const auto describe = [&](const std::pair<const std::string, OwnAttack> *a_attack) {
            if (!a_attack) {
                return std::string("none");
            }
            const float margin = recoverIn - a_attack->second.startup;
            return std::format("'{}' {:.2f}s, forward {:.0f}: {}", a_attack->first, a_attack->second.startup,
                               a_attack->second.forward,
                               margin > 0.0f ? std::format("lands {:.2f}s before you recover", margin)
                                             : std::format("too slow by {:.2f}s", -margin));
        };
        LOG_DEBUG("Punish 0x{:08X}: your '{}' blows past, you recover in {:.2f}s ({}){}; dist {:.0f}, reach {:.0f} + "
                  "target {:.0f}; its fastest attack {}; fastest in reach {}; {} of {} known attacks fit {}",
                  static_cast<std::uint32_t>(a_npc->GetFormID()), clipName, recoverIn,
                  marked ? "MCO_Recovery" : "clip end, no recovery mark",
                  windowIn >= 0.0f ? std::format(", combo window in {:.2f}s", windowIn) : std::string(), a_distance,
                  a_reach, a_targetRadius, describe(fastest), describe(fastestInReach), fit,
                  own != g_ownAttacks.end() ? own->second.size() : 0, SwingTag(a_attacker->GetFormID(), a_swing));
    }

    void Evict(RE::FormID a_formID)
    {
        g_ownLast.erase(a_formID); // main thread, like ObserveOwnAttack
        g_ownAttacks.erase(a_formID);
        std::scoped_lock lock(g_lock);
        g_tracks.erase(a_formID);
    }
} // namespace CombatAI::ClipTiming
