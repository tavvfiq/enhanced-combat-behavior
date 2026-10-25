#pragma once

namespace CombatAI
{
    namespace Hooks
    {
        // Install all hooks
        void Install();

        // True once the dagger-block patch is in (daggers run the Block tree like other one-handed weapons)
        bool DaggerBlockPatched();
    } // namespace Hooks
} // namespace CombatAI
