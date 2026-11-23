"""Disassemble runLoop opcode handlers, located through kLabels' RELATIVE relocations."""
import re, subprocess, sys
binary, outdir, tag = sys.argv[1], sys.argv[2], sys.argv[3]
OPS = {"PUSH_INSTVAR": 26, "STORE_INSTVAR": 27, "PUSH_OWN_INSTVAR": 42, "IVAR_GROUP": 43,
       "IVAR_GROUP_END": 44, "PUSH_CONST_INSTVAR": 65}
nm = subprocess.run(["nm", binary], capture_output=True, text=True).stdout
kl = next(int(l.split()[0], 16) for l in nm.splitlines() if "runLoop" in l and "kLabels" in l)
rl = next(int(l.split()[0], 16) for l in nm.splitlines()
          if l.endswith("_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE"))
rel = subprocess.run(["readelf", "-rW", binary], capture_output=True, text=True).stdout
table = {}
for l in rel.splitlines():
    f = l.split()
    if len(f) >= 4 and "R_X86_64_RELATIVE" in l:
        off = int(f[0], 16)
        if kl <= off < kl + 256 * 8:
            table[(off - kl) // 8] = int(f[-1], 16)
assert len(table) == 256, len(table)
starts = sorted(set(table.values()))
for name, op in OPS.items():
    a = table[op]
    b = next(x for x in starts if x > a)
    dis = subprocess.run(["objdump", "-d", "--no-show-raw-insn", "-M", "intel",
                          f"--start-address={a:#x}", f"--stop-address={b:#x}", binary],
                         capture_output=True, text=True).stdout
    lines = [l for l in dis.splitlines() if re.match(r"^\s+[0-9a-f]+:", l)]
    with open(f"{outdir}/{tag}-{name}.raw.s", "w") as fh:
        fh.write("\n".join(lines) + "\n")
    # Normalised: offsets relative to the handler start, targets relative to runLoop.
    norm = []
    for l in lines:
        addr, ins = l.split(":", 1)
        ins = re.sub(r"\s+", " ", ins.strip())
        ins = re.sub(r"\b([0-9a-f]{5,}) <([^>]+)>",
                     lambda m: f"<{m.group(2)}>", ins)
        norm.append(f"+{int(addr, 16) - a:4x}  {ins}")
    with open(f"{outdir}/{tag}-{name}.s", "w") as fh:
        fh.write("\n".join(norm) + "\n")
    print(f"{tag} {name:<20} op {op:3d} at {a:#x} (runLoop+{a - rl:#x}), {b - a} bytes, {len(lines)} insns")
