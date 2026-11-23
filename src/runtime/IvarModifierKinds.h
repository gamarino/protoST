#pragma once
// The kinds of instance-variable modifier (explicit-state spec §6) and the
// text forms the compiler and the runtime share: the names, the `kind:name`
// record of plan decision D-a, and the generated constructor's selector.
// Header-only and free of protoCore, so the frontend (AST.h, the compiler)
// can use it without depending on the runtime library. The run-time lookups
// on class objects are in IvarModifiers.h, which includes this header.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace protoST {

enum class IvarModifier : uint8_t { Plain, Constant, WriteOnce, Atomic };

// "plain", "constant", "writeOnce", "atomic".
inline const char* modifierName(IvarModifier m) {
    switch (m) {
    case IvarModifier::Plain:     return "plain";
    case IvarModifier::Constant:  return "constant";
    case IvarModifier::WriteOnce: return "writeOnce";
    case IvarModifier::Atomic:    return "atomic";
    }
    return "plain";
}

// The inverse of modifierName; false for any other text.
inline bool parseModifierName(std::string_view text, IvarModifier& out) {
    for (IvarModifier m : {IvarModifier::Plain, IvarModifier::Constant, IvarModifier::WriteOnce,
                           IvarModifier::Atomic}) {
        if (text == modifierName(m)) {
            out = m;
            return true;
        }
    }
    return false;
}

// The whitespace-separated words of `text`, in order: the names of a
// declaration clause, or the entries of a `__ivar_modifiers__` record.
inline std::vector<std::string> splitNames(std::string_view text) {
    std::vector<std::string> out;
    std::string word;
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') {
            if (!word.empty()) out.push_back(std::move(word));
            word.clear();
        } else {
            word += c;
        }
    }
    if (!word.empty()) out.push_back(std::move(word));
    return out;
}

// One `kind:name` entry of a class's `__ivar_modifiers__`.
struct ModifiedIvar {
    std::string name;
    IvarModifier modifier;
};

// The modifier `records` give `name`; Plain when they do not list it.
inline IvarModifier modifierIn(const std::vector<ModifiedIvar>& records, std::string_view name) {
    for (const ModifiedIvar& e : records)
        if (e.name == name) return e.modifier;
    return IvarModifier::Plain;
}

// The `__ivar_modifiers__` text for `names` and the aligned `modifiers`
// (plain variables are left out; empty when every variable is plain).
inline std::string encodeModifiers(const std::vector<std::string>& names,
                                   const std::vector<IvarModifier>& modifiers) {
    std::string out;
    for (std::size_t i = 0; i < names.size() && i < modifiers.size(); ++i) {
        if (modifiers[i] == IvarModifier::Plain) continue;
        if (!out.empty()) out += ' ';
        out += modifierName(modifiers[i]);
        out += ':';
        out += names[i];
    }
    return out;
}

// The keyword selector over `constants`: {"number","owner"} -> "number:owner:".
// Empty for no constants.
inline std::string constructorSelectorFor(const std::vector<std::string>& constants) {
    std::string sel;
    for (const auto& c : constants) {
        sel += c;
        sel += ':';
    }
    return sel;
}

// Why `:=` to a modified instance variable is refused (explicit-state spec
// §12): M1, M2 and M4.
enum class AssignmentRefusal : uint8_t {
    ConstantOutsideInitialize,   // M1
    ConstantInsideBlock,         // M2
    AtomicAssigned,              // M4
};

// The text of a refused `:=` to the variable `name` in a method of the class
// `className`, with the compiler's ` (line N)` suffix. The compiler reports
// it at compile time and install-time resolution of a late method signals it,
// so both paths give the same words.
inline std::string assignmentRefusalText(AssignmentRefusal why, const std::string& name,
                                         const std::string& className, int line) {
    std::string text;
    switch (why) {
    case AssignmentRefusal::ConstantOutsideInitialize:
        text = "cannot assign to the constant '" + name + "' of " + className + " outside initialize";
        break;
    case AssignmentRefusal::ConstantInsideBlock:
        text = "cannot assign to the constant '" + name + "' of " + className
               + " inside a block: the block may run after construction";
        break;
    case AssignmentRefusal::AtomicAssigned:
        text = "cannot assign to the atomic variable '" + name + "' of " + className
               + ": use update:with: or compareAndSet:from:to:";
        break;
    }
    return text + " (line " + std::to_string(line) + ")";
}

} // namespace protoST
