#pragma once

// Forward declarations to avoid pulling protoCore headers everywhere.
namespace proto {
    class ProtoSpace;
    class ProtoContext;
    class ProtoObject;
}

namespace protoST {

// Minimal Smalltalk-flavoured prototype hierarchy.
//
// For F2 we hand-roll the base prototypes from C++ so that literal values
// produced by the runtime have a stable parent chain.  Primitive methods
// (#+, #=, #printString, ...) attach in Tasks 40-43.
//
//     Object
//       |-- Number
//       |     |-- SmallInteger
//       |     |-- LargeInteger
//       |     `-- Float
//       |-- Boolean
//       |-- String
//       |     `-- Symbol
//       |-- Block
//       `-- UndefinedObject (nil)
//
// Each pointer is owned by the ProtoSpace; we only hold non-owning views.
struct Bootstrap {
    const proto::ProtoObject* objectProto        = nullptr;
    const proto::ProtoObject* numberProto        = nullptr;
    const proto::ProtoObject* smallIntegerProto  = nullptr;
    const proto::ProtoObject* largeIntegerProto  = nullptr;
    const proto::ProtoObject* floatProto         = nullptr;
    const proto::ProtoObject* booleanProto       = nullptr;
    const proto::ProtoObject* stringProto        = nullptr;
    const proto::ProtoObject* symbolProto        = nullptr;
    const proto::ProtoObject* blockProto         = nullptr;
    const proto::ProtoObject* nilProto           = nullptr;
};

// Build the prototype tree on top of `sp.objectPrototype` and bind the result
// into the relevant protoCore primitive slots so that literals dispatch
// through the Smalltalk chain.
void bootstrapPrototypes(proto::ProtoSpace& sp, proto::ProtoContext* ctx, Bootstrap& out);

} // namespace protoST
