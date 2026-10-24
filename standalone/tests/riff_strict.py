# SPDX-License-Identifier: MPL-2.0
"""Parser RIFF rigoroso per i test BreathClick.

Verifica:
- RIFF size == file_size - 8;
- ogni chunk ha header, dati e padding fisico se la dimensione e' dispari;
- nessun byte finale estraneo o mancante;
- opzionalmente stampa l'ordine dei chunk.
"""
import struct


def parse(path):
    raw = open(path, "rb").read()
    assert len(raw) >= 12, "file troppo corto"
    assert raw[0:4] == b"RIFF", "manca RIFF"
    riff_size = struct.unpack("<I", raw[4:8])[0]
    assert riff_size == len(raw) - 8, (
        f"RIFF size {riff_size} != file_size-8 {len(raw) - 8}")
    assert raw[8:12] == b"WAVE", "manca WAVE"
    chunks = []
    pos = 12
    while pos < len(raw):
        assert pos + 8 <= len(raw), f"header chunk troncato a {pos}"
        cid = raw[pos:pos + 4]
        size = struct.unpack("<I", raw[pos + 4:pos + 8])[0]
        data_start = pos + 8
        data_end = data_start + size
        assert data_end <= len(raw), f"chunk {cid!r} troncato"
        if size & 1:
            assert data_end < len(raw), f"padding mancante per {cid!r}"
            pad = raw[data_end:data_end + 1]
            assert pad == b"\0", f"padding non nullo per {cid!r}: {pad!r}"
            pos = data_end + 1
        else:
            pos = data_end
        chunks.append((cid.decode("latin1"), size))
    assert pos == len(raw), "byte finali oltre i chunk"
    return chunks


if __name__ == "__main__":
    import sys
    for name in sys.argv[1:]:
        print(name, parse(name))
