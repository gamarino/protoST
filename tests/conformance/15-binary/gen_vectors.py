#!/usr/bin/env python3
"""Turn protoIO's shared JSON test vectors into protoST module files.

    python3 tests/conformance/15-binary/gen_vectors.py [vectors-dir]

reads ../protoIO/tests/vectors/*.json (the byte-compatibility oracle made by
CPython's standard library and msgpack-python) and writes, next to this
script, one module _vectors_<name>.st per file. A test imports a module and
asks its class for the entries:

    lib := Import from: '_vectors_packing'.
    lib VectorsPacking all do: [:entry | ...].

The output is committed and deterministic: a rerun leaves `git diff` clean.
The files start with "EXPECT: generated" so the conformance runner treats
each as a (trivially passing) program; they define classes only.

Entry layouts (every entry is an Array; bytes are ByteArrays, hex is decoded):

  packing     all: #(kind width bigEndian value bytes)
                kind is the String 'u', 'i', 'f32' or 'f64'. For 'u'/'i' value
                is an Integer (a LargeInteger beyond 2^55); for floats it is
                the IEEE 754 bit pattern as a String of big-endian hex.
  base64      all: #(data text)
  crc32       all: #(data init crc)
              combine: #(a b crcA crcB lengthOfB combined)
  blake2b     all: #(data key size digest)
  sha256_hmac all: #('sha256' data digest) or #('hmac' key data mac)
  msgpack     all: #(description bytes value); value is a tagged tree:
                #('nil'), #('bool' b), #('int' n), #('f64' bitsHex),
                #('str' s), #('bin' bytes), #('array' elements),
                #('map' pairs) with pairs an Array of #(key value), and
                #('ext' type bytes).

A vector too large for the JSON file is a descriptor
{"pattern": "seq", "length": N}: N bytes where byte i is i % 251. The module
then builds it with its class method seq: (no literal is emitted), and the
entry is written as a brace Array instead of a literal one.
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_VECTORS = HERE.parents[2].parent / "protoIO" / "tests" / "vectors"
SEQ_PERIOD = 251


def is_seq(x):
    return isinstance(x, dict) and x.get("pattern") == "seq"


class Emitter:
    """Renders values as Smalltalk source: literal form, or expression form
    (brace Arrays and `self seq:` sends) for an entry holding a descriptor."""

    def __init__(self, class_name):
        self.class_name = class_name

    @staticmethod
    def string(s):
        for ch in s:
            if ord(ch) < 32 or ord(ch) == 127:
                raise ValueError("control character in a vector string: %r" % s)
        return "'" + s.replace("'", "''") + "'"

    @staticmethod
    def bytes_literal(hex_text):
        data = bytes.fromhex(hex_text)
        return "#[" + " ".join(str(b) for b in data) + "]"

    def data(self, x, expr):
        """A `data` field: hex string or seq descriptor."""
        if is_seq(x):
            if not expr:
                raise ValueError("seq descriptor in literal context")
            return "(%s seq: %d)" % (self.class_name, x["length"])
        return self.bytes_literal(x)

    @staticmethod
    def has_seq(x):
        if is_seq(x):
            return True
        if isinstance(x, dict):
            return any(Emitter.has_seq(v) for v in x.values())
        if isinstance(x, list):
            return any(Emitter.has_seq(v) for v in x)
        return False

    def array(self, items, expr):
        if expr:
            return "{" + ". ".join(items) + "}"
        return "#(" + " ".join(items) + ")"

    def msgpack_value(self, node, expr):
        t = node["t"]
        if t == "nil":
            return self.array([self.string("nil")], expr)
        if t == "bool":
            return self.array([self.string("bool"), "true" if node["v"] else "false"], expr)
        if t == "int":
            return self.array([self.string("int"), str(int(node["v"]))], expr)
        if t == "f64":
            return self.array([self.string("f64"), self.string(node["bits"])], expr)
        if t == "str":
            return self.array([self.string("str"), self.string(node["v"])], expr)
        if t == "bin":
            if is_seq(node):
                payload = self.data(node, expr)
            else:
                payload = self.bytes_literal(node["v"])
            return self.array([self.string("bin"), payload], expr)
        if t == "array":
            return self.array([self.string("array"),
                               self.array([self.msgpack_value(v, expr) for v in node["v"]], expr)], expr)
        if t == "map":
            pairs = [self.array([self.msgpack_value(k, expr), self.msgpack_value(v, expr)], expr)
                     for k, v in node["v"]]
            return self.array([self.string("map"), self.array(pairs, expr)], expr)
        if t == "ext":
            return self.array([self.string("ext"), str(node["type"]),
                               self.bytes_literal(node["v"])], expr)
        raise ValueError("unknown msgpack node type %r" % t)


def entries_packing(e, em):
    if e["kind"] in ("u", "i"):
        value = str(int(e["value"]))
    else:
        value = em.string(e["value"])
    return [em.string(e["kind"]), str(e["width"]), "true" if e["big_endian"] else "false",
            value, em.bytes_literal(e["bytes"])]


def entries_base64(e, em):
    return [em.bytes_literal(e["data"]), em.string(e["text"])]


def entries_blake2b(e, em):
    return [em.bytes_literal(e["data"]), em.bytes_literal(e["key"]), str(e["size"]),
            em.bytes_literal(e["digest"])]


def entries_sha256_hmac(e, em):
    if "hmac" in e:
        return [em.string("hmac"), em.bytes_literal(e["key"]), em.bytes_literal(e["data"]),
                em.bytes_literal(e["hmac"])]
    return [em.string("sha256"), em.bytes_literal(e["data"]), em.bytes_literal(e["sha256"])]


def entry_crc(e, em, expr):
    return [em.data(e["data"], expr), str(e["init"]), str(e["crc"])]


def entry_combine(e, em):
    return [em.bytes_literal(e["a_hex"]), em.bytes_literal(e["b_hex"]), str(e["crc_a"]),
            str(e["crc_b"]), str(e["len_b"]), str(e["combined"])]


def entry_msgpack(e, em, expr):
    return [em.string(e["description"]), em.bytes_literal(e["bytes"]),
            em.msgpack_value(e["value"], expr)]


def method(selector, doc, rows, class_name):
    """A class method answering an Array of entries; each row is already
    rendered source for one entry."""
    body = ".\n".join("    " + row for row in rows)
    return "\n".join([class_name + " class >> " + selector,
                      '  "' + doc + '"',
                      "  ^ {", body, "  }."])


SEQ_METHOD = '''{cls} class >> seq: aLength
  "aLength bytes where byte i (from 0) is i \\\\ 251, as protoIO's vector files describe a large input."
  | unit stream |
  unit := ByteArray withAll: ((0 to: 250) asArray).
  stream := WriteStream on: ByteArray new.
  (aLength // 251) timesRepeat: [stream nextPutAll: unit].
  stream nextPutAll: (unit copyFrom: 1 to: aLength \\\\ 251).
  ^ stream contents.'''


def generate(src_dir, out_dir):
    names = sorted(p.stem for p in src_dir.glob("*.json"))
    if not names:
        raise SystemExit("no vector files in %s" % src_dir)
    written = []
    for name in names:
        raw = json.loads((src_dir / (name + ".json")).read_text(encoding="utf-8"))
        cls = "Vectors" + "".join(part.capitalize() for part in name.split("_"))
        em = Emitter(cls)
        uses_seq = False

        def rows_for(items, fields_fn):
            """Each row picks literal or expression form by its own content."""
            nonlocal uses_seq
            rows = []
            for e in items:
                expr = Emitter.has_seq(e)
                uses_seq = uses_seq or expr
                rows.append(em.array(fields_fn(e, expr), expr))
            return rows

        doc = {
            "packing": "Integer and IEEE 754 packing: #(kind width bigEndian value bytes).",
            "base64": "Base64: #(data text).",
            "crc32": "CRC-32: all answers #(data init crc); combine answers #(a b crcA crcB lengthOfB combined).",
            "blake2b": "BLAKE2b: #(data key size digest).",
            "sha256_hmac": "SHA-256 and HMAC-SHA-256: #('sha256' data digest) or #('hmac' key data mac).",
            "msgpack": "MessagePack: #(description bytes value), value a tagged tree.",
        }[name]
        if name == "packing":
            methods = [("all", "Every entry.", rows_for(raw, lambda e, x: entries_packing(e, em)))]
        elif name == "base64":
            methods = [("all", "Every entry.", rows_for(raw, lambda e, x: entries_base64(e, em)))]
        elif name == "blake2b":
            methods = [("all", "Every entry.", rows_for(raw, lambda e, x: entries_blake2b(e, em)))]
        elif name == "sha256_hmac":
            methods = [("all", "Every entry.", rows_for(raw, lambda e, x: entries_sha256_hmac(e, em)))]
        elif name == "crc32":
            methods = [("all", "The crc entries.", rows_for(raw["crc"], lambda e, x: entry_crc(e, em, x))),
                       ("combine", "The combine entries.",
                        rows_for(raw["combine"], lambda e, x: entry_combine(e, em)))]
        elif name == "msgpack":
            methods = [("all", "Every entry.", rows_for(raw, lambda e, x: entry_msgpack(e, em, x)))]
        else:
            raise SystemExit("no layout for vector file %s.json: add one to gen_vectors.py" % name)

        out = ['"EXPECT: generated"',
               '"Generated by gen_vectors.py from protoIO tests/vectors/%s.json. Do not edit."' % name,
               '"%s"' % doc,
               "Object subclass: #%s instanceVariableNames: ''." % cls, ""]
        if uses_seq:
            out += [SEQ_METHOD.format(cls=cls), ""]
        for selector, mdoc, rows in methods:
            out += [method(selector, mdoc, rows, cls), ""]
        path = out_dir / ("_vectors_%s.st" % name)
        path.write_text("\n".join(out), encoding="utf-8", newline="\n")
        written.append((path.name, sum(len(r) for _, _, r in methods)))
    return written


def main(argv):
    src = Path(argv[1]) if len(argv) > 1 else DEFAULT_VECTORS
    for fname, count in generate(src, HERE):
        print("%s: %d entries" % (fname, count))


if __name__ == "__main__":
    main(sys.argv)
