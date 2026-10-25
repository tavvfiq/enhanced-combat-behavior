#include "TimedBlockIntegration.h"
#include "ActorUtils.h"
#include "Logger.h"
#include "pch.h"
#include <st-magic.h>

namespace CombatAI
{
    bool TimedBlockIntegration::IsModLoaded() const
    {
        auto dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            LOG_DEBUG("TESDataHandler not available");
            return false;
        }

        // Try LookupModByName first (checks if mod exists in data handler)
        // This searches the raw files list and should work during kDataLoaded
        auto timedBlockMod = dataHandler->LookupModByName("SimpleTimedBlock.esp");
        if (timedBlockMod) {
            LOG_DEBUG("Found SimpleTimedBlock.esp via LookupModByName");
            return true;
        }

        // Try LookupLoadedModByName (checks compiled file collection)
        // This might work if compiledFileCollection is ready
        timedBlockMod = dataHandler->LookupLoadedModByName("SimpleTimedBlock.esp");
        if (timedBlockMod) {
            LOG_DEBUG("Found SimpleTimedBlock.esp via LookupLoadedModByName");
            return true;
        }

        // Fallback: Try to look up a form from the mod
        // This is the most reliable if forms are loaded
        constexpr RE::FormID ID_PARRY_WINDOW_SPELL = 0x802;
        constexpr const char *MOD_FILE = "SimpleTimedBlock.esp";
        auto testForm = dataHandler->LookupForm<RE::SpellItem>(ID_PARRY_WINDOW_SPELL, MOD_FILE);
        if (testForm) {
            LOG_DEBUG("Found SimpleTimedBlock.esp via form lookup");
            return true;
        }

        LOG_DEBUG("SimpleTimedBlock.esp not found via any method");
        return false;
    }

    bool TimedBlockIntegration::Initialize()
    {
        if (!IsModLoaded()) {
            LOG_INFO("Simple Timed Block not found: timed blocks run as well-timed plain blocks (no window effect)");
            m_isAvailable = false;
            return false;
        }

        auto dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            LOG_WARN("Failed to get TESDataHandler for Timed Block integration");
            m_isAvailable = false;
            return false;
        }

        // Lookup forms from SimpleTimedBlock.esp
        // FormIDs from mod-data.h:
        // ID_PARRY_WINDOW_SPELL = 0x802
        // ID_PARRY_WINDOW_EFFECT = 0x801
        constexpr RE::FormID ID_PARRY_WINDOW_SPELL = 0x802;
        constexpr RE::FormID ID_PARRY_WINDOW_EFFECT = 0x801;
        constexpr const char *MOD_FILE = "SimpleTimedBlock.esp";

        m_spellParryWindow = dataHandler->LookupForm<RE::SpellItem>(ID_PARRY_WINDOW_SPELL, MOD_FILE);
        m_mgefParryWindow = dataHandler->LookupForm<RE::EffectSetting>(ID_PARRY_WINDOW_EFFECT, MOD_FILE);

        if (!m_spellParryWindow) {
            LOG_WARN("Failed to find spell_parry_window (FormID 0x802) from SimpleTimedBlock.esp");
            m_isAvailable = false;
            return false;
        }

        if (!m_mgefParryWindow) {
            LOG_WARN("Failed to find mgef_parry_window (FormID 0x801) from SimpleTimedBlock.esp");
            m_isAvailable = false;
            return false;
        }

        m_isAvailable = true;
        LOG_INFO("Timed Block integration initialized successfully");
        return true;
    }

    bool TimedBlockIntegration::ApplyTimedBlockWindow(RE::Actor *a_actor)
    {
        if (!m_isAvailable || !m_spellParryWindow || !a_actor) {
            return false;
        }

        // Apply the spell that creates the timed block window
        // This spell applies the mgef_parry_window effect for 0.33 seconds
        // Use styyx-util's MagicUtil::ApplySpell which handles both permanent and temporary spells
        StyyxUtil::MagicUtil::ApplySpell(a_actor, a_actor, m_spellParryWindow);
        return true;
    }

    bool TimedBlockIntegration::HasTimedBlockWindow(RE::Actor *a_actor) const
    {
        if (!m_isAvailable || !m_mgefParryWindow || !a_actor) {
            return false;
        }

        // Check if actor has the timed block window effect active
        return ActorUtils::SafeHasMagicEffect(a_actor, m_mgefParryWindow);
    }
} // namespace CombatAI
