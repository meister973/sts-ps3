#!/usr/bin/env python3
"""Pack a folder into a debug PS3 PKG, byte-compatible with PSL1GHT's `pkg`.

PSL1GHT's tool aborts past ~1024 entries and this game ships ~3,700 asset
files, so this re-implements the same format (PSL1GHT tools/sfo_pkg pkg.c,
MIT, "Copyright (c) 2011 PSL1GHT Development Team") without that limit, and
streams files instead of holding the whole package in memory. For the same
input folder the output is byte-identical to PSL1GHT's.

usage: make_pkg.py <content-id> <folder> <out.pkg>
"""
import hashlib
import os
import struct
import sys

TYPE_NPDRMSELF = 0x1
TYPE_RAW = 0x3
TYPE_DIRECTORY = 0x4
OVERWRITE_ALLOWED = 0x80000000
ENTRY_SIZE = 0x20
SELF_MAGIC = b"SCE\x00\x00\x00\x00\x02\x80"
CHUNK_BLOCKS = 1 << 16


class Entry:
    def __init__(self, name, source, size, flags):
        self.name = name.encode("utf-8")
        self.source = source
        self.size = size
        self.flags = flags
        self.name_offset = 0
        self.data_offset = 0

    @property
    def is_directory(self):
        return self.flags & 0xFF == TYPE_DIRECTORY


def align16(value):
    return (value + 0x0F) & ~0x0F


def collect(folder, root, entries):
    """Files of a folder first, then each subfolder followed by its contents,
    both sorted by full path, which is the order PSL1GHT's tool produces."""
    paths = [os.path.join(folder, name) for name in os.listdir(folder)]
    files = sorted(p for p in paths if not os.path.isdir(p))
    folders = sorted(p for p in paths if os.path.isdir(p))
    for path in files:
        name = os.path.relpath(path, root).replace(os.sep, "/")
        size = os.path.getsize(path)
        flags = OVERWRITE_ALLOWED | TYPE_RAW
        if name == "USRDIR/EBOOT.BIN":
            size = ((size - 0x30 + 63) & ~63) + 0x30
            flags = OVERWRITE_ALLOWED | TYPE_NPDRMSELF
        entries.append(Entry(name, path, size, flags))
    for path in folders:
        name = os.path.relpath(path, root).replace(os.sep, "/")
        entries.append(Entry(name, None, 0, OVERWRITE_ALLOWED | TYPE_DIRECTORY))
        collect(path, root, entries)


def layout(entries):
    cursor = ENTRY_SIZE * len(entries)
    for entry in entries:
        entry.name_offset = cursor
        cursor += align16(len(entry.name))
    for entry in entries:
        entry.data_offset = cursor
        if not entry.is_directory:
            cursor += align16(entry.size)
    return cursor


def descriptor_area(entries):
    table = b"".join(struct.pack(">IIQQII", e.name_offset, len(e.name), e.data_offset, e.size, e.flags, 0) for e in entries)
    names = b"".join(e.name + b"\0" * (align16(len(e.name)) - len(e.name)) for e in entries)
    return table + names


def eboot_payload(data, content_id):
    """An NPDRM SELF carries an 0x80-byte NPD block after its digest table."""
    if len(data) < 0x68 or data[:9] != SELF_MAGIC:
        return data
    app_info_offset, = struct.unpack(">Q", data[40:48])
    digest_offset, = struct.unpack(">Q", data[88:96])
    app_type = 0
    if app_info_offset + 0x18 <= len(data):
        app_type, = struct.unpack(">I", data[app_info_offset + 12:app_info_offset + 16])
    found = False
    while digest_offset + 0x10 <= len(data):
        kind, size, has_next = struct.unpack(">IIQ", data[digest_offset:digest_offset + 16])
        if kind == 3:
            found = True
            break
        digest_offset += size
        if has_next != 1:
            break
    digest_offset += 0x10
    if app_type != 8 or not found:
        return data
    file_sha1 = hashlib.sha1(data).digest()
    meta = bytearray(0x80)
    meta[0:16] = struct.pack(">IIII", 0x4E504400, 1, 3, 1)
    cid = content_id.encode()[:0x30]
    meta[16:16 + len(cid)] = cid
    for j in range(16):
        meta[0x40 + j] = file_sha1[j]
        meta[0x50 + j] = ~file_sha1[j] & 0xFF
        meta[0x60 + j] = ((1 if j == 15 else 0) ^ meta[0x50 + j] ^ 0xAA) & 0xFF
    return data[:digest_offset] + bytes(meta) + data[digest_offset + 0x80:]


def entry_payloads(entries, content_id):
    for entry in entries:
        if entry.is_directory:
            continue
        with open(entry.source, "rb") as handle:
            data = handle.read()
        if entry.flags & 0xFF == TYPE_NPDRMSELF:
            data = eboot_payload(data, content_id)
        yield data + b"\0" * max(0, align16(entry.size) - len(data))


class Cipher:
    """PKG stream cipher: block n is SHA-1(key context with counter n)[:16]."""

    def __init__(self, key, counter=0):
        prefix = key[0:8] + key[0:8] + key[8:16] + key[8:16] + b"\0" * 24
        self.prefix = hashlib.sha1(prefix)
        self.counter = counter
        self.pending = b""

    def _keystream(self, blocks):
        parts = []
        for n in range(self.counter, self.counter + blocks):
            h = self.prefix.copy()
            h.update(struct.pack(">Q", n & 0xFFFFFFFFFFFFFFFF))
            parts.append(h.digest()[:16])
        self.counter += blocks
        return b"".join(parts)

    def crypt(self, data):
        stream = self._keystream((len(data) + 15) // 16)[:len(data)]
        mixed = int.from_bytes(data, "big") ^ int.from_bytes(stream, "big")
        return mixed.to_bytes(len(data), "big")


def stream_encrypted(out, cipher, chunks):
    """Encrypt a byte stream, keeping block boundaries across chunks."""
    carry = b""
    for chunk in chunks:
        carry += chunk
        whole = len(carry) // 16 * 16
        for start in range(0, whole, CHUNK_BLOCKS * 16):
            end = min(whole, start + CHUNK_BLOCKS * 16)
            out.write(cipher.crypt(carry[start:end]))
        carry = carry[whole:]
    if carry:
        out.write(cipher.crypt(carry))


def build_header(item_count, data_size, content_id, qa_digest):
    header = bytearray(0x80)
    header[0:48] = struct.pack(">IIIIIIQQQ", 0x7F504B47, 1, 0xC0, 5, 0x80, item_count, data_size + 0x1A0, 0x140, data_size)
    cid = content_id.encode()[:0x30]
    header[48:48 + len(cid)] = cid
    header[0x60:0x70] = qa_digest
    header[0x70:0x80] = Cipher(qa_digest, 0xFFFFFFFFFFFFFFFF).crypt(b"\0" * 16)
    return bytes(header)


def build_meta(data_size):
    return struct.pack(">IIIIIIIIIIIHHIIIHH", 1, 4, 3, 2, 4, 5, 3, 4, 0x0E, 4, 8, 0, 0, data_size, 5, 4, 0x1061, 0)


def pack(content_id, folder, output):
    root = os.path.abspath(folder)
    entries = []
    collect(root, root, entries)
    data_size = layout(entries)
    descriptors = descriptor_area(entries)

    # QA digest: original file bytes, the first 0x30 header bytes (identity
    # fields, sizes still zero), 0x50 zero bytes, then the descriptor area.
    digest = hashlib.sha1()
    for entry in entries:
        if not entry.is_directory:
            with open(entry.source, "rb") as handle:
                for block in iter(lambda: handle.read(1 << 20), b""):
                    digest.update(block)
    identity = struct.pack(">IIIIIIQQQ", 0x7F504B47, 1, 0xC0, 5, 0x80, len(entries), data_size + 0x1A0, 0x140, data_size)
    digest.update(identity)
    digest.update(b"\0" * 0x50)
    digest.update(descriptors)
    qa_digest = digest.digest()[:16]

    header = build_header(len(entries), data_size, content_id, qa_digest)
    meta = build_meta(data_size)
    header_sha = hashlib.sha1(header).digest()[3:19]
    meta_sha = hashlib.sha1(meta).digest()[3:19]
    meta_pad = Cipher(meta_sha).crypt(b"\0" * 0x30)
    with open(output, "wb") as out:
        out.write(header)
        out.write(header_sha)
        out.write(Cipher(header_sha).crypt(meta_pad))
        out.write(meta)
        out.write(meta_sha)
        out.write(meta_pad)
        stream_encrypted(out, Cipher(qa_digest), [descriptors, *entry_payloads(entries, content_id)])
        out.write(b"\0" * 0x60)
    print(f"[make_pkg] {len(entries)} items, {data_size} data bytes -> {output}")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__.strip().splitlines()[-1])
    pack(sys.argv[1], sys.argv[2], sys.argv[3])
