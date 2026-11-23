#include "runtime/ConstantConstructor.h"

#include "protoST/STRuntime.h"
#include "runtime/BlockCode.h"
#include "runtime/Bootstrap.h"
#include "runtime/BytecodeModule.h"
#include "runtime/InstanceTemplate.h"
#include "runtime/IvarModifiers.h"
#include "runtime/NativeError.h"
#include "runtime/NativeExceptionBridge.h"
#include "runtime/TransientPin.h"
#include "runtime/ValueFormat.h"
#include "protoCore.h"

#include <algorithm>
#include <memory>

namespace protoST {

// object_prims.cpp: a send from C++ (a user method runs in a nested engine).
const proto::ProtoObject* sendDynamic(STRuntime& rt, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* recv,
                                      const proto::ProtoString* selector,
                                      const proto::ProtoObject* const* args, int argc,
                                      bool* understood);

namespace {

// The name a class object carries (`__class_name__`), or "" for none.
std::string classNameOf(const Bootstrap& b, proto::ProtoContext* ctx,
                        const proto::ProtoObject* cls) {
    const proto::ProtoObject* n = cls->getOwnAttributeDirect(ctx, b.sym.className);
    const proto::ProtoString* str = n && n != PROTO_NONE ? n->asString(ctx) : nullptr;
    return str ? str->toStdString(ctx) : std::string();
}

// The String or Symbol a class records under `key` (own only), or "".
std::string ownText(proto::ProtoContext* ctx, const proto::ProtoObject* o,
                    const proto::ProtoString* key) {
    const proto::ProtoObject* v = o->getOwnAttributeDirect(ctx, key);
    const proto::ProtoString* str = v && v != PROTO_NONE ? v->asString(ctx) : nullptr;
    return str ? str->toStdString(ctx) : std::string();
}

const proto::ProtoString* ivarKey(proto::ProtoContext* ctx, const std::string& name) {
    return proto::ProtoString::createSymbol(ctx, ("_iv_" + name).c_str());
}

// The keywords of a keyword selector, without their colons: `number:owner:`
// gives {number, owner}. Empty when the selector is not a keyword selector.
std::vector<std::string> keywordsOf(const std::string& selector) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : selector) {
        if (c != ':') { cur += c; continue; }
        if (cur.empty()) return {};
        out.push_back(cur);
        cur.clear();
    }
    if (!cur.empty()) return {};
    return out;
}

// The end-of-construction check over `constants`, already computed.
const proto::ProtoObject* checkSet(STRuntime& rt, proto::ProtoContext* ctx,
                                   const std::vector<std::string>& constants,
                                   const proto::ProtoObject* instance,
                                   const std::string& selector) {
    for (const std::string& name : constants) {
        const proto::ProtoObject* v = instance->getOwnAttributeDirect(ctx, ivarKey(ctx, name));
        if (v && v != PROTO_NONE) continue;
        // M13 (spec §12), signalled in place (pre-flight ruling C1): a
        // handler's answer is what the construction answers.
        const proto::ProtoObject* answer = signalErrorNamed(rt, ctx, "Error",
            "the constant '" + name + "' of " + articleAndClassName(rt, ctx, instance)
            + " is not set at the end of construction (" + selector + ")");
        return answer ? answer : PROTO_NONE;
    }
    return instance;
}

} // namespace

const proto::ProtoString* constantConstructorKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__constant_constructor__");
}

std::string constantConstructorOf(STRuntime&, proto::ProtoContext* ctx,
                                  const proto::ProtoObject* cls) {
    if (!cls || cls == PROTO_NONE || cls->isInteger(ctx) || cls->isFloat(ctx) || cls->asString(ctx))
        return {};
    const proto::ProtoObject* v = cls->getAttribute(ctx, constantConstructorKey(ctx));
    const proto::ProtoString* str = v && v != PROTO_NONE ? v->asString(ctx) : nullptr;
    return str ? str->toStdString(ctx) : std::string();
}

void installConstantConstructor(STRuntime& rt, proto::ProtoContext* ctx,
                                const proto::ProtoObject* cls) {
    const Bootstrap& b = rt.bootstrap();
    if (!cls || cls == PROTO_NONE || isMetaclass(b, ctx, cls)) return;
    const std::vector<std::string> constants = constantNamesOf(rt, ctx, cls);
    const std::string selector = constants.empty() ? std::string()
                                                   : constructorSelectorFor(constants);
    const proto::ProtoString* key = constantConstructorKey(ctx);
    auto* mcls = const_cast<proto::ProtoObject*>(cls);
    const std::string previous = ownText(ctx, cls, key);
    if (previous.empty() && constants.empty()) return;   // every class without constants
    const proto::ProtoObject* meta = ensureMetaclass(b, ctx, cls);

    // A constructor this class recorded under another selector is stale: its
    // constants changed (a redeclaration, or addBehavior:, which copies the
    // class's own records and class-side methods). M9 kept any user method
    // off that selector while it was recorded, so the method there is the
    // generated one.
    if (!previous.empty() && previous != selector) {
        meta->removeAttribute(ctx, proto::ProtoString::createSymbol(ctx, previous.c_str()));
        mcls->removeAttribute(ctx, key);
    }
    if (constants.empty()) return;

    // The body (plan decision D-d, pre-flight ruling C11). Local 0 is self;
    // the arguments are locals 1..n.
    const unsigned int n = static_cast<unsigned int>(constants.size());
    auto module = std::make_unique<BytecodeModule>();
    module->setArgCount(static_cast<int>(n) + 1);
    module->emit(Op::PUSH_SELF, 0);
    for (unsigned int i = 1; i <= n; ++i) module->emitWide(Op::PUSH_LOCAL, i);
    module->emitWide(Op::MAKE_ARRAY, n);
    module->emitWide(Op::PUSH_CONST, static_cast<unsigned int>(module->internSymbol(selector)));
    module->emitWide(Op::SEND_KEYWORD, static_cast<unsigned int>(
                                           module->internSymbol("__constructConstants:selector:")));
    module->emit(Op::RETURN, 0);
    std::vector<std::string> localNames{"self"};
    localNames.insert(localNames.end(), constants.begin(), constants.end());
    module->setLocalNames(std::move(localNames));
    const std::string className = classNameOf(b, ctx, cls);
    module->setDefiningClass(className);
    module->setDebugName(className + " class>>" + selector);
    module->setSourceName("<generated constructor>");
    const BytecodeModule& code = rt.adoptModule(std::move(module));

    // The method object, as PUSH_BLOCK makes one for a compiled method
    // (ExecutionEngine::newClosure): a clone of the closure template with
    // its four slots written at once. A method frame is its own home and
    // takes the receiver as self, so neither slot matters here; no captured
    // dictionary.
    const proto::ProtoObject* codeRef = blockCodeRefFor(rt, ctx, code);
    TransientPin pinCode(ctx, codeRef);
    const proto::ProtoString* names[4] = {
        b.sym.bcPtr, b.sym.captured, b.sym.homeFrame, b.sym.blockSelf };
    const proto::ProtoObject* values[4] = { codeRef, PROTO_NONE, ctx->fromLong(0), PROTO_NONE };
    const proto::ProtoObject* method = b.closureTemplate->setAttributes(ctx, 4, names, values);
    TransientPin pinMethod(ctx, method);

    const proto::ProtoString* selectorSym = proto::ProtoString::createSymbol(ctx, selector.c_str());
    meta->setAttribute(ctx, selectorSym, method);
    mcls->setAttribute(ctx, key, selectorSym->asObject(ctx));
}

const proto::ProtoObject* checkConstantsSet(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* cls,
                                            const proto::ProtoObject* instance,
                                            const std::string& selector) {
    return checkSet(rt, ctx, constantNamesOf(rt, ctx, cls), instance, selector);
}

const proto::ProtoObject* constructWithConstants(STRuntime& rt, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* cls,
                                                 const proto::ProtoObject* args,
                                                 const std::string& selector) {
    const Bootstrap& b = rt.bootstrap();
    const char* who = "__constructConstants:selector:";
    const std::vector<std::string> constants = constantNamesOf(rt, ctx, cls);
    const std::vector<std::string> names = keywordsOf(selector);
    if (names.empty())
        throw NativeError(std::string(who) + " expects a keyword selector, not '" + selector + "'");
    for (const std::string& name : names)
        if (std::find(constants.begin(), constants.end(), name) == constants.end())
            throw NativeError(std::string(who) + " '" + name + "' is not a constant of "
                              + articleAndClassName(rt, ctx, cls));
    const proto::ProtoObject* dataObj =
        args && args != PROTO_NONE ? args->getAttribute(ctx, b.sym.data) : nullptr;
    const proto::ProtoList* values =
        dataObj && dataObj != PROTO_NONE ? dataObj->asList(ctx) : nullptr;
    if (!values || values->getSize(ctx) != names.size())
        throw NativeError(std::string(who) + " expects an Array of "
                          + std::to_string(names.size()) + " values");

    // 1. The instance (as basicNew makes it).
    const proto::ProtoObject* instance = newInstance(rt, ctx, cls);
    TransientPin pinInstance(ctx, instance);

    // 2. One write of the given constants, on the fresh, unpublished
    //    object; never nil (§6.2): a nil argument leaves its constant unset.
    std::vector<const proto::ProtoString*> keys;
    std::vector<const proto::ProtoObject*> given;
    keys.reserve(names.size());
    given.reserve(names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        const proto::ProtoObject* v = values->getAt(ctx, listIndex(i));
        if (!v || v == PROTO_NONE) continue;
        keys.push_back(ivarKey(ctx, names[i]));
        given.push_back(v);
    }
    if (!keys.empty())
        instance->setAttributes(ctx, static_cast<unsigned>(keys.size()), keys.data(), given.data());

    // 3. initialize, which reads the given constants freely.
    bool understood = false;
    sendDynamic(rt, ctx, instance, b.sym.initialize,
                nullptr, 0, &understood);

    // 4. The end-of-construction check, naming this constructor (M13).
    return checkSet(rt, ctx, constants, instance, selector);
}

} // namespace protoST
