"""Retain only the selected owner-local algorithm table; never publish it."""

from pathlib import Path
import hashlib
import json
import struct

R = Path(__file__).resolve().parents[2]
b = (R / "Wow.exe").read_bytes()
identity = hashlib.sha256(b).hexdigest()
if identity != "bf644876709c591acc17c0da8cdf1814edcc9f1e6bc109a8c0d5c38c79dc953c":
    raise ValueError("particle algorithm table address is not verified for this executable")
p = struct.unpack_from("<I", b, 60)[0]
if b[p : p + 4] != b"PE\0\0":
    raise ValueError("PE signature")
s = p + 24 + struct.unpack_from("<H", b, p + 20)[0]
rva = 0x9F1700 - 0x400000
for i in range(struct.unpack_from("<H", b, p + 6)[0]):
    _, va, size, raw = struct.unpack_from("<4I", b, s + i * 40 + 8)
    if va <= rva and rva + 256 <= va + size:
        table = b[raw + rva - va : raw + rva - va + 256]
        if len(table) != 256:
            raise ValueError("truncated table")
        out = R / "engine/particle-draw/build/random-table.private.bin"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(table)
        print(
            json.dumps(
                {
                    "status": "PREPARED_OWNER_LOCAL_PARTICLE_ALGORITHM_TABLE",
                    "bytes": len(table),
                    "exe_sha256": identity,
                    "table_sha256": hashlib.sha256(table).hexdigest(),
                    "output": str(out.relative_to(R)),
                }
            )
        )
        break
else:
    raise ValueError("table RVA not backed by regular section bytes")
