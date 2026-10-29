#include "ClipTiming.h"
#include "Config.h"
#include "Logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <mutex>
#include <string>
#include <unordered_map>
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
        ReadResult Read(RE::Actor *a_actor, Clip &a_out)
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
        const auto result = Read(a_attacker, clip);

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

    Eta Predict(RE::Actor *a_attacker, float &a_earliest, float &a_latest)
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
        // Every blow of the clip is past: none is coming from it. The median rule would still see one (1.7.1 run: 3
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

    void Evict(RE::FormID a_formID)
    {
        std::scoped_lock lock(g_lock);
        g_tracks.erase(a_formID);
    }
} // namespace CombatAI::ClipTiming
