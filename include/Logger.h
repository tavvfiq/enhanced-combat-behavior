#pragma once

#include "pch.h"

#include <format>
#include <string>

namespace logger = SKSE::log;

namespace CombatAI
{
// Convenience macros
#define LOG_INFO(...) logger::info(__VA_ARGS__)
#define LOG_ERROR(...) logger::error(__VA_ARGS__)
#define LOG_WARN(...) logger::warn(__VA_ARGS__)
#define LOG_DEBUG(...) logger::debug(__VA_ARGS__)

    void ConsolePrint(const char *a_fmt, ...);

    // The swing a debug line is about: the attacker and the observing NPC's count of its swings. Every line about one
    // swing seen by one NPC (Eta, Decision, Intent, Executed, Timing, HitTiming, ClipTiming) carries the same tag next
    // to that NPC's id, so a log search pairs them exactly
    inline std::string SwingTag(RE::FormID a_attacker, std::uint32_t a_swing)
    {
        return std::format("swing={:08X}#{}", static_cast<std::uint32_t>(a_attacker), a_swing);
    }

    // The swing tag of the NPC decision running on this thread (CombatDirector sets it for a selected NPC whose target
    // is swinging; empty otherwise): lines logged deep in the decision (Intent posts) append it
    inline thread_local std::string t_swingTag;
} // namespace CombatAI
