#include "TreeNodes.h"
#include "Logger.h"

namespace CombatAI::TreeNodes
{
    namespace
    {
        void *AllocRaw(std::size_t a_size)
        {
            // Vanilla tree builders initialize the node pool before their first allocation
            // (SE 48151+0x2E / AE 49182+0x2C); do the same in case ours run first.
            auto *flag = reinterpret_cast<std::int32_t *>(Id::nodePoolFlag.address());
            if (*flag != 2) {
                REL::Relocation<void (*)(void *, std::int32_t *)> init{Id::nodePoolInit};
                init(reinterpret_cast<void *>(Id::nodePool.address()), flag);
            }
            REL::Relocation<void *(*)(void *, std::uint32_t, std::uint32_t, std::uint32_t)> alloc{Id::nodeAllocator};
            return alloc(reinterpret_cast<void *>(Id::nodePool.address()), static_cast<std::uint32_t>(a_size), 0, 0);
        }

        // Allocate, zero, base-construct, stamp the class vtable. The pool hands back dirty
        // memory and the base ctor only sets its own fields; vanilla zeroes first too.
        Node *Create(std::size_t a_size, const REL::RelocationID &a_vtable)
        {
            auto *p = static_cast<Node *>(AllocRaw(a_size));
            if (!p) {
                LOG_ERROR("Tree: node allocation failed (size 0x{:X})", a_size);
                return nullptr;
            }
            std::memset(p, 0, a_size);
            REL::Relocation<void *(*)(void *)> ctor{Id::ctorNonvirtual};
            ctor(p);
            *reinterpret_cast<std::uintptr_t *>(p) = a_vtable.address();
            return p;
        }

        Node *Factory(const REL::RelocationID &a_id)
        {
            REL::Relocation<Node *(*)()> f{a_id};
            return f();
        }

        template <class T> void Poke(Node *a_node, std::size_t a_offset, T a_value)
        {
            *reinterpret_cast<T *>(reinterpret_cast<std::uintptr_t>(a_node) + a_offset) = a_value;
        }

        // CreateContextNode2 (size 0x38): GetCombatItem member fn @0x28, equip slot @0x30
        // (vanilla: Melee 0, Block 1).
        Node *Context(const REL::RelocationID &a_vtable, std::uint32_t a_slot)
        {
            auto *n = Create(0x38, a_vtable);
            if (n) {
                Poke(n, 0x28, reinterpret_cast<void *>(Id::getCombatItem.address()));
                Poke(n, 0x30, a_slot);
            }
            return n;
        }

        struct FactoryExtra
        {
            void *func1;
            void *func2;
        };
    } // namespace

    Node *Repeat() { return Factory(Id::repeatNew); }
    Node *NextSelector() { return Factory(Id::nextSelectorNew); }
    Node *Bash() { return Factory(Id::bashNew); }
    Node *Attack() { return Create(0x28, Id::vtAttack); }
    Node *SpecialAttack() { return Create(0x28, Id::vtSpecialAttack); }
    Node *Block() { return Create(0x28, Id::vtBlock); }
    Node *BlockAttack() { return Create(0x28, Id::vtBlockAttack); }
    Node *Advance() { return Create(0x28, Id::vtAdvance); }
    Node *Circle() { return Create(0x28, Id::vtCircle); }
    Node *Fallback() { return Create(0x28, Id::vtFallback); }
    Node *Backoff() { return Create(0x28, Id::vtBackoff); }

    // Idle (size 0x30) holds for the float at +0x28 before the tree re-selects.
    Node *Idle(float a_holdSeconds)
    {
        auto *n = Create(0x30, Id::vtIdle);
        if (n) {
            Poke(n, 0x28, a_holdSeconds);
        }
        return n;
    }

    Node *ContextMelee() { return Context(Id::vtContextMelee, 0); }
    Node *ContextBlock() { return Context(Id::vtContextBlock, 1); }

    Node *RandomNode(Node *a_child, Probability a_probability, const char *a_name)
    {
        // The engine's own wrapper factory builds the expression object and attaches a_child;
        // the returned array's first entry is a_child, whose parent is the new ConditionalNode.
        using Wrapper = RE::BSTArray<Node *> &(*)(RE::BSTArray<Node *> &, const char *, FactoryExtra *, Node *);
        REL::Relocation<Wrapper> wrapper{Id::randomNodeNew};
        FactoryExtra extra{reinterpret_cast<void *>(a_probability), nullptr};
        RE::BSTArray<Node *> result;
        wrapper(result, a_name, &extra, a_child);
        if (result.empty() || !result[0] || !result[0]->parent) {
            LOG_ERROR("Tree: RandomNode factory returned no wrapper for {}", a_name);
            return nullptr;
        }
        return result[0]->parent;
    }

    bool Is(const Node *a_node, const REL::RelocationID &a_vtable)
    {
        return a_node && reinterpret_cast<std::uintptr_t>(a_node->vtable) == a_vtable.address();
    }

    Node *Named(Node *a_node, const char *a_name)
    {
        if (a_node) {
            a_node->name = a_name;
        }
        return a_node;
    }

    Node *AddChild(Node *a_parent, Node *a_child)
    {
        REL::Relocation<void (*)(Node *, Node *)> f{Id::addFirstChild};
        f(a_parent, a_child);
        return a_parent;
    }

    void SetChildren(Node *a_parent, std::initializer_list<Node *> a_children)
    {
        RE::BSTArray<Node *> items;
        for (auto *c : a_children) {
            items.push_back(c);
        }
        REL::Relocation<void (*)(Node *, RE::BSTArray<Node *> &)> f{Id::initChildrensParents};
        f(a_parent, items);
    }

    void SetRoot(void *a_tree, Node *a_root)
    {
        REL::Relocation<void (*)(void *, Node *)> f{Id::initNodes};
        f(a_tree, a_root);
    }
} // namespace CombatAI::TreeNodes
