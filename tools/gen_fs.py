#!/usr/bin/env python3
"""
gen_fs.py — Generates a C source file containing a pre-formatted FAT12
filesystem image. The C file places the image in the .flash_fs linker
section, which CMakeLists.txt pins to FLASH_DISK_OFFSET (0x10040000) via
--section-start so picotool includes it in the normal bidule-01.uf2.

Usage: gen_fs.py <output.c> [file1.bdb file2.bdb ...]

Constants must stay in sync with usb_msc.c.
"""

import sys
import struct
import os

FLASH_DISK_OFFSET    = 512 * 1024               # must match usb_msc.c
FLASH_SIZE_BYTES     = 2 * 1024 * 1024
DISK_BLOCK_SIZE      = 512
DISK_BLOCK_COUNT     = (FLASH_SIZE_BYTES - FLASH_DISK_OFFSET) // DISK_BLOCK_SIZE

FAT_SECTOR_COUNT     = 3
ROOT_DIR_SECTORS     = 32
SECTORS_PER_CLUSTER  = 8
FORMAT_SERIAL        = bytes([0x42, 0x44, 0x01, 0x00])

RESERVED_SECTORS     = 1
FAT_COUNT            = 2
ROOT_DIR_ENTRIES     = ROOT_DIR_SECTORS * DISK_BLOCK_SIZE // 32   # 512

DATA_START_SECTOR    = RESERVED_SECTORS + FAT_COUNT * FAT_SECTOR_COUNT + ROOT_DIR_SECTORS
CLUSTER_SIZE         = SECTORS_PER_CLUSTER * DISK_BLOCK_SIZE       # 4096 bytes
MAX_DATA_CLUSTERS    = (DISK_BLOCK_COUNT - DATA_START_SECTOR) // SECTORS_PER_CLUSTER

# ── FAT12 helpers ─────────────────────────────────────────────────────────────

def set_fat12_entry(fat: bytearray, cluster: int, value: int):
    offset = cluster + cluster // 2
    if cluster % 2 == 0:
        fat[offset]     =  value & 0xFF
        fat[offset + 1] = (fat[offset + 1] & 0xF0) | ((value >> 8) & 0x0F)
    else:
        fat[offset]     = (fat[offset] & 0x0F) | ((value & 0x0F) << 4)
        fat[offset + 1] = (value >> 4) & 0xFF

def name_to_83(filename: str) -> bytes:
    base, _, ext = filename.upper().rpartition('.')
    if not _:
        base, ext = filename.upper(), ''
    return (base[:8].ljust(8) + ext[:3].ljust(3)).encode('ascii')

def make_dir_entry(name83: bytes, first_cluster: int, file_size: int) -> bytes:
    entry = bytearray(32)
    entry[0:11] = name83
    entry[11]   = 0x20                                    # ATTR_ARCHIVE
    struct.pack_into('<H', entry, 26, first_cluster & 0xFFFF)
    struct.pack_into('<I', entry, 28, file_size)
    return bytes(entry)

# ── FAT12 image ───────────────────────────────────────────────────────────────

def make_boot_sector() -> bytes:
    bs = bytearray(512)
    bs[0:3]   = b'\xEB\x3C\x90'
    bs[3:11]  = b'MSDOS5.0'
    struct.pack_into('<H', bs, 11, DISK_BLOCK_SIZE)
    bs[13]    = SECTORS_PER_CLUSTER
    struct.pack_into('<H', bs, 14, RESERVED_SECTORS)
    bs[16]    = FAT_COUNT
    struct.pack_into('<H', bs, 17, ROOT_DIR_ENTRIES)
    struct.pack_into('<H', bs, 19, DISK_BLOCK_COUNT)
    bs[21]    = 0xF8
    struct.pack_into('<H', bs, 22, FAT_SECTOR_COUNT)
    struct.pack_into('<H', bs, 24, 1)
    struct.pack_into('<H', bs, 26, 1)
    struct.pack_into('<I', bs, 28, 0)
    struct.pack_into('<I', bs, 32, 0)
    bs[36]    = 0x80
    bs[37]    = 0x00
    bs[38]    = 0x29
    bs[39:43] = FORMAT_SERIAL
    bs[43:54] = b'BIDULE01   '
    bs[54:62] = b'FAT12   '
    bs[510]   = 0x55
    bs[511]   = 0xAA
    return bytes(bs)

def make_fs_image(files: list) -> bytes:
    """
    files: list of (filename, data_bytes) tuples, pre-loaded by caller.
    """
    fat_bytes = FAT_SECTOR_COUNT * DISK_BLOCK_SIZE
    fat  = bytearray(fat_bytes)
    fat[0] = 0xF8; fat[1] = 0xFF; fat[2] = 0xFF     # media descriptor + EOC

    root = bytearray(ROOT_DIR_SECTORS * DISK_BLOCK_SIZE)
    root[0:11] = b'BIDULE01   '
    root[11]   = 0x08                                 # ATTR_VOLUME_ID

    data = bytearray()
    next_cluster    = 2
    dir_entry_offset = 32                             # first slot after volume label

    for filename, file_data in files:
        if not file_data:
            continue

        num_clusters = (len(file_data) + CLUSTER_SIZE - 1) // CLUSTER_SIZE
        if next_cluster + num_clusters - 1 > MAX_DATA_CLUSTERS + 1:
            sys.exit(f"[gen_fs] ERROR: not enough space for {filename}")

        first_cluster = next_cluster

        for i in range(num_clusters):
            value = (first_cluster + i + 1) if i < num_clusters - 1 else 0xFFF
            set_fat12_entry(fat, first_cluster + i, value)

        next_cluster += num_clusters

        padded = file_data + b'\x00' * (num_clusters * CLUSTER_SIZE - len(file_data))
        data += padded

        if dir_entry_offset + 32 > len(root):
            sys.exit(f"[gen_fs] ERROR: root directory full, cannot add {filename}")

        root[dir_entry_offset:dir_entry_offset + 32] = make_dir_entry(
            name_to_83(filename), first_cluster, len(file_data)
        )
        dir_entry_offset += 32

        print(f"[gen_fs] + {filename} ({len(file_data)} B, cluster {first_cluster}, {num_clusters} cluster(s))")

    return make_boot_sector() + bytes(fat) + bytes(fat) + bytes(root) + bytes(data)

# ── C emission ────────────────────────────────────────────────────────────────

def emit_c(image: bytes, path: str):
    flash_addr = 0x10000000 + FLASH_DISK_OFFSET
    lines = [
        "/* Auto-generated by tools/gen_fs.py — do not edit. */",
        f"/* FAT12 image for XIP flash address 0x{flash_addr:08X} (FLASH_DISK_OFFSET). */",
        "#include <stdint.h>",
        "",
        "/* Placed in .flash_fs by CMakeLists.txt --section-start linker option.",
        "   -u flash_fs_image prevents --gc-sections from discarding it. */",
        "__attribute__((section(\".flash_fs\"), used))",
        f"const uint8_t flash_fs_image[{len(image)}] = {{",
    ]
    for i in range(0, len(image), 16):
        chunk = image[i:i + 16]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    lines += ["};", ""]
    with open(path, "w") as f:
        f.write("\n".join(lines))

# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    if len(sys.argv) < 2:
        sys.exit(f"Usage: {sys.argv[0]} <output.c> [file.bdb ...]")

    output_c   = sys.argv[1]
    file_paths = sys.argv[2:]

    files = []
    for path in file_paths:
        with open(path, 'rb') as f:
            data = f.read()
        files.append((os.path.basename(path), data))

    image = make_fs_image(files)
    emit_c(image, output_c)
    print(f"[gen_fs] {len(image)} bytes -> {output_c}")
    print(f"[gen_fs] boot[0:4]  = {image[:4].hex(' ')}")
    print(f"[gen_fs] boot[510:] = {image[510:512].hex(' ')}")

if __name__ == "__main__":
    main()
