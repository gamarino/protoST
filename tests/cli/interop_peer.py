#!/usr/bin/env python3
"""CPython side of the protoST binary-storage interop test.

Usage: interop_peer.py write <dir> | check <dir>

Everything is encoded the way ProtoBase encodes it: msgpack.packb(...,
use_bin_type=True), unpackb(..., raw=False, strict_map_key=True), zlib.crc32,
hashlib.blake2b(digest_size=...), struct big/little endian. The record frame
is a simplified one, payload_len:u32 LE | crc32:u32 LE | payload; ProtoBase's
exact record format belongs to protoBaseST.

  write  creates, in <dir>: data.msgpack, facts_py.bin, records_py.bin
  check  verifies what protoST wrote back into <dir>: back.msgpack,
         out.msgpack, facts_st.bin, records_st.bin
"""
import hashlib
import struct
import sys
import zlib

import msgpack
from msgpack import ExtType


def sample():
    """A nested value with every MessagePack type and a multi-key map whose
    keys are deliberately not sorted (so the key order is observable)."""
    return {
        "z_nil": None,
        "a_true": True,
        "m_false": False,
        "ints": [0, 1, 127, 128, 255, 256, 65535, 65536, 2**32 - 1, 2**32, 2**63 - 1, 2**63, 2**64 - 1,
                 -1, -32, -33, -128, -129, -32768, -32769, -2**31, -2**31 - 1, -2**63],
        "floats": [1.5, -0.0, 3.141592653589793, 1e300],
        "text": "añ€ plain",
        "bin": b"\x00\xff\x10",
        "empty_bin": b"",
        "ext": ExtType(5, b"xyz"),
        "nested": {"k2": [1, [2, [3, {}]]], "k1": {"deep": []}},
        "": "empty key",
    }


def pack(value):
    return msgpack.packb(value, use_bin_type=True)


def unpack(data):
    return msgpack.unpackb(data, raw=False, strict_map_key=True)


def facts(data):
    """Fixed-layout numbers and digests of `data`."""
    out = struct.pack(">BHIQ", 200, 51234, 4000000000, 2**63 + 5)
    out += struct.pack("<bhiqfd", -100, -30000, -2000000000, -2**62, 1.5, -2.25e100)
    out += struct.pack(">qfd", -2**63, -0.5, 2.5e-300)
    out += struct.pack(">I", zlib.crc32(data))
    out += hashlib.blake2b(data, digest_size=20).digest()
    out += hashlib.blake2b(data, digest_size=32, key=b"k" * 16).digest()
    out += hashlib.sha256(data).digest()
    return out


def records():
    """Fifty framed records; sizes 0, 6, 12, ... 294."""
    out = b""
    for i in range(50):
        payload = bytes((i * j + 7) % 256 for j in range(i * 6))
        out += struct.pack("<II", len(payload), zlib.crc32(payload)) + payload
    return out


def read(path):
    with open(path, "rb") as f:
        return f.read()


def fail(message):
    print("FAIL: " + message)
    sys.exit(1)


def verify_frames(data, expected_count):
    pos, n = 0, 0
    while pos < len(data):
        if pos + 8 > len(data):
            fail("truncated frame header at %d" % pos)
        length, crc = struct.unpack_from("<II", data, pos)
        payload = data[pos + 8:pos + 8 + length]
        if len(payload) != length:
            fail("truncated payload at %d" % pos)
        if zlib.crc32(payload) != crc:
            fail("bad CRC32 in the frame at %d" % pos)
        pos += 8 + length
        n += 1
    if n != expected_count:
        fail("%d frames, expected %d" % (n, expected_count))


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("write", "check"):
        fail("usage: interop_peer.py write|check <dir>")
    d = sys.argv[2]
    if sys.argv[1] == "write":
        data = pack(sample())
        open(d + "/data.msgpack", "wb").write(data)
        open(d + "/facts_py.bin", "wb").write(facts(data))
        open(d + "/records_py.bin", "wb").write(records())
        return
    original = read(d + "/data.msgpack")
    back = read(d + "/back.msgpack")
    if unpack(back) != unpack(original):
        fail("back.msgpack decodes to another value")
    if back != original:
        fail("back.msgpack differs byte for byte from the original (%d vs %d bytes)" % (len(back), len(original)))
    out = read(d + "/out.msgpack")
    if unpack(out) != sample():
        fail("out.msgpack decodes to another value than the sample")
    if out != pack(sample()):
        fail("out.msgpack differs byte for byte from packb of the sample")
    if read(d + "/facts_st.bin") != facts(original):
        fail("facts_st.bin differs from the struct/zlib/hashlib bytes")
    st_records = read(d + "/records_st.bin")
    verify_frames(st_records, 50)
    if st_records != records():
        fail("records_st.bin differs from the reference frames")
    print("PY-OK")


main()
