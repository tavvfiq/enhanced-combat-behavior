#pragma once

#include "pch.h"

// Behavior-tree node layer: builds trees out of the engine's own node classes.
// Ported from FenixCombatAI-NG (src/CombatBehaviorNodes.h + Offsets.h, the user's port). IDs are
// RELOCATION_ID(SE 1.5.97, AE); AE 1.6.1170 and 1.7.104 share them for everything used here
// (checked with skyrim_re/re/fenixport.py, 2026-10-22). Every vtable ID was RTTI-checked on SE,
// 1.6.1170 and 1.7.104.
namespace CombatAI::TreeNodes
{
    namespace Id
    {
        // engine helpers
        inline constexpr REL::RelocationID ctorNonvirtual{46301, 47545};   // CombatBehaviorTreeNode base ctor
        inline constexpr REL::RelocationID nodeAllocator{66859, 68115};    // pool allocate(pool, size, 0, 0)
        inline constexpr REL::RelocationID nodePoolInit{66862, 68121};     // pool init(pool, &flag)
        inline constexpr REL::RelocationID nodePool{514110, 400188};       // pool singleton
        inline constexpr REL::RelocationID nodePoolFlag{514112, 400190};   // 2 once the pool is initialized
        inline constexpr REL::RelocationID initNodes{46263, 47518};        // tree.root = new RootNode(child)
        inline constexpr REL::RelocationID addFirstChild{46304, 47548};
        inline constexpr REL::RelocationID initChildrensParents{46305, 47549};
        inline constexpr REL::RelocationID getCombatItem{32458, 33197};    // context member fn @0x28

        // node factories
        inline constexpr REL::RelocationID repeatNew{32409, 47690};
        inline constexpr REL::RelocationID nextSelectorNew{32407, 47934};  // FallbackSelector<NextChildSelector>
        inline constexpr REL::RelocationID bashNew{46646, 47846};
        inline constexpr REL::RelocationID randomNodeNew{46641, 47845};    // ConditionalNode<RandomBoolChance(f(Attacker,Target))>

        // node vtables (factories inlined away on AE)
        inline constexpr REL::RelocationID vtAttack{266747, 213789};
        inline constexpr REL::RelocationID vtSpecialAttack{266746, 213776};
        inline constexpr REL::RelocationID vtBlock{265987, 212608};
        inline constexpr REL::RelocationID vtBlockAttack{265985, 212582};
        inline constexpr REL::RelocationID vtIdle{256701, 205318};
        inline constexpr REL::RelocationID vtContextMelee{266745, 213763};
        inline constexpr REL::RelocationID vtContextBlock{265984, 212569};
        inline constexpr REL::RelocationID vtContextCloseMovement{266093, 212655};
        inline constexpr REL::RelocationID vtRepeat{256697, 205278};
        inline constexpr REL::RelocationID vtAdvance{266096, 212694};
        inline constexpr REL::RelocationID vtCircle{266102, 212772};
        inline constexpr REL::RelocationID vtFallback{266101, 212759};
        inline constexpr REL::RelocationID vtBackoff{266103, 212785};
    } // namespace Id

    // Base engine node (size 0x28).
    struct Node
    {
        void *vtable;            // 0x00
        RE::BSFixedString name;  // 0x08
        Node *parent;            // 0x10
        Node **children;         // 0x18
        std::int32_t childCount; // 0x20
        std::uint32_t pad24;     // 0x24
    };
    static_assert(sizeof(Node) == 0x28);

    // float(attacker, target): the probability a RandomNode rolls against.
    using Probability = float (*)(RE::Actor *, RE::Actor *);

    Node *Repeat();
    Node *NextSelector();
    Node *Bash();
    Node *Attack();
    Node *SpecialAttack();
    Node *Block();
    Node *BlockAttack();
    Node *Idle(float a_holdSeconds);
    Node *Advance();
    Node *Circle();
    Node *Fallback();
    Node *Backoff();
    Node *ContextMelee();
    Node *ContextBlock();
    // ConditionalNode that rolls a_probability(attacker, target) and runs a_child on success.
    Node *RandomNode(Node *a_child, Probability a_probability, const char *a_name);

    // True if a_node is an instance of the class whose vtable is a_vtable.
    bool Is(const Node *a_node, const REL::RelocationID &a_vtable);
    Node *Named(Node *a_node, const char *a_name);
    Node *AddChild(Node *a_parent, Node *a_child); // single-child nodes; returns the parent
    // Children must be detached (parent == nullptr): the engine adds each one's top ancestor.
    void SetChildren(Node *a_parent, std::initializer_list<Node *> a_children);
    void SetRoot(void *a_tree, Node *a_root);
} // namespace CombatAI::TreeNodes
