#pragma once

#include "pch.h"

namespace CombatAI
{
    // Integration with Simple Timed Block mod
    class TimedBlockIntegration
    {
      public:
        static TimedBlockIntegration &GetInstance()
        {
            static TimedBlockIntegration instance;
            return instance;
        }

        TimedBlockIntegration(const TimedBlockIntegration &) = delete;
        TimedBlockIntegration &operator=(const TimedBlockIntegration &) = delete;

        // Initialize Timed Block integration (lookup forms)
        bool Initialize();

        // Check if Timed Block mod is available
        bool IsAvailable() const { return m_isAvailable; }

        // Apply timed block window spell to actor (creates 0.33s window)
        bool ApplyTimedBlockWindow(RE::Actor *a_actor);

      private:
        TimedBlockIntegration() = default;
        ~TimedBlockIntegration() = default;

        bool m_isAvailable = false;

        // Forms from SimpleTimedBlock.esp
        RE::SpellItem *m_spellParryWindow = nullptr;    // FormID 0x802 - Spell that applies timed block window
        RE::EffectSetting *m_mgefParryWindow = nullptr; // FormID 0x801 - Magic effect indicating window is active

        // Check if mod is loaded
        bool IsModLoaded() const;
    };
} // namespace CombatAI
