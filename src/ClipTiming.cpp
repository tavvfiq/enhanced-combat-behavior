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
        };

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
                    found = true;
                    foundAhead = ahead;
                }
            }
            return found ? ReadResult::kFound : ReadResult::kNoHitFrame;
        }

        // Per attacker: the clip seen last, and the predictions made for its next blow
        struct Track
        {
            Clip clip;
            Clock::time_point at{};
            float rate = 0.0f; // measured clip seconds per second (0 = not measured yet)
            // Earliest prediction for the next blow made with a measured rate, and the brain's at that same tick
            bool predicted = false;
            Clock::time_point clipAt{}, medianAt{};
            bool hasMedian = false;
            float horizon = 0.0f; // seconds before the predicted blow the prediction was made
            int hitIndex = -1;    // which HitFrame it was for
            bool debug = false;
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
        } g_stats;

        std::string Brief(const std::vector<float> &a_hits)
        {
            std::string out;
            for (const float h : a_hits) {
                out += std::format("{}{:.2f}", out.empty() ? "" : ",", h);
            }
            return out;
        }
    } // namespace

    void Sample(RE::Actor *a_attacker, float a_medianTimeToHit, bool a_debug)
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
                return; // another NPC sampled this attacker this frame
            }
        }
        Clip clip;
        const auto result = Read(a_attacker, clip);

        std::scoped_lock lock(g_lock);
        auto &track = g_tracks[id];
        if (result != ReadResult::kFound) {
            if (a_debug && track.clip.animation) {
                LOG_DEBUG("ClipTiming 0x{:08X}: swing with no attack clip found ({})", id,
                          result == ReadResult::kNoGraph ? "no graph" : "no active clip with a HitFrame");
            }
            track.clip = {};
            track.at = now;
            return;
        }
        const bool sameClip = clip.animation == track.clip.animation && clip.localTime >= track.clip.localTime;
        if (!sameClip) {
            // A new swing (or the same clip restarted): predictions start over
            track.rate = 0.0f;
            track.predicted = false;
            track.debug = a_debug;
            if (a_debug) {
                bool firstPerson = false;
                if (a_attacker->IsPlayerRef()) {
                    const auto *camera = RE::PlayerCamera::GetSingleton();
                    firstPerson = camera && camera->IsInFirstPerson();
                }
                LOG_DEBUG("ClipTiming 0x{:08X}: clip '{}' ({:.2f}s, HitFrame {}, playbackSpeed {:.2f}, graph {}{}) at "
                          "{:.2f}s",
                          id, clip.name, clip.duration, Brief(clip.hits), clip.playbackSpeed, clip.graph,
                          firstPerson ? ", first person" : "", clip.localTime);
            }
        } else {
            const float dt = Since(track.at, now);
            if (dt > 0.01f && clip.localTime > track.clip.localTime) {
                track.rate = (clip.localTime - track.clip.localTime) / dt;
            }
            track.debug |= a_debug;
        }
        track.clip = std::move(clip);
        track.at = now;

        // The next HitFrame ahead of the clip's position, once the rate is measured
        if (track.rate <= 0.0f) {
            return;
        }
        int next = -1;
        for (int i = 0; i < static_cast<int>(track.clip.hits.size()); ++i) {
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
            LOG_DEBUG("ClipTiming 0x{:08X}: at {:.2f}s, rate {:.2f} -> HitFrame {:.2f} in {:.2f}s (median rule: {})", id,
                      track.clip.localTime, track.rate, track.clip.hits[next], ahead,
                      track.hasMedian ? std::format("{:.2f}s", a_medianTimeToHit) : std::string("none"));
        }
    }

    void OnHit(RE::Actor *a_attacker, RE::Actor *a_victim, bool a_power)
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
        track.lastVictim = a_victim->GetFormID();
        track.lastBlowAt = now;
        ++g_stats.blows;
        if (!track.clip.animation) {
            ++g_stats.noClip;
            return;
        }
        if (!track.predicted) {
            ++g_stats.noPrediction; // the blow came before two samples of its clip
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
                          "{:.2f}): that swing missed, not scored",
                          a_attacker->GetFormID(), a_victim->GetFormID(), a_power ? "power" : "light", clipError,
                          track.clip.name, track.clip.hits[track.hitIndex]);
            }
            track.predicted = false;
            return;
        }
        ++g_stats.compared;
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
                      "ahead, off by {:+.2f}s; median rule {}",
                      a_attacker->GetFormID(), a_victim->GetFormID(), a_power ? "power" : "light", track.clip.name,
                      track.clip.hits[track.hitIndex], track.horizon, clipError,
                      track.hasMedian ? std::format("off by {:+.2f}s", medianError) : std::string("had none"));
        }
        track.predicted = false; // a multi-hit clip predicts its next HitFrame afresh
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
                 "{}, too fast to predict {}, after a missed prediction {}",
                 s.blows, s.compared, s.compared ? s.horizonSum / s.compared : 0.0, clipMean, clipSd,
                 s.compared ? s.clipAbs / s.compared : 0.0, medianMean, medianSd,
                 s.medianCount ? s.medianAbs / s.medianCount : 0.0, s.medianCount, s.noClip, s.noPrediction, s.stale);
    }

    void Evict(RE::FormID a_formID)
    {
        std::scoped_lock lock(g_lock);
        g_tracks.erase(a_formID);
    }
} // namespace CombatAI::ClipTiming
