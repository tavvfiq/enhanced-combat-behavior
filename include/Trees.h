#pragma once

// Our Melee and Block behavior trees. They replace the vanilla tree builders and are made of
// vanilla node classes, so vanilla's leaf nodes still execute every attack, block and bash.
// Each branch rolls vanilla's own chance unless a brain intent (Intents.h) overrides it.
namespace CombatAI::Trees
{
    // Patch the vanilla Melee/Block tree builders. Call from SKSEPlugin_Load: the engine builds
    // these trees before kDataLoaded.
    void Install();

    // End a direct block (DualBlock node) and send blockStop, once; a_force ends it even if the hold isn't over.
    void ReleaseDualBlock(RE::Actor *a_actor, bool a_force);
} // namespace CombatAI::Trees
