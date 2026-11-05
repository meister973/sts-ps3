#!/usr/bin/env python3
"""Adapt stock TeaVM C runtime to PSL1GHT's POSIX newlib and malloc heap.

Reuses architecture-independent upstream Vita bug fixes, while retaining
native 64-bit pointers and supplying PS3 clock/thread implementations.
"""
import importlib.util
import sys
from pathlib import Path

gen = Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("vita_fixes", Path(__file__).resolve().parents[1] / "tools/vita_patch_runtime.py")
fix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fix)

def replace(name, old, new):
    path = gen / name
    text = path.read_text()
    if new in text:
        return
    if old not in text:
        raise SystemExit(f"{name}: missing patch anchor {old!r}")
    path.write_text(text.replace(old, new, 1))

replace("definitions.h", "#ifndef TEAVM_USE_SETJMP",
        "#ifdef TEAVM_PS3\n#undef TEAVM_UNIX\n#define TEAVM_UNIX 0\n#undef TEAVM_PSP\n#define TEAVM_PSP 1\n#endif\n\n#ifndef TEAVM_USE_SETJMP")
replace("fiber.c", "#include <pspkernel.h>", '#include "stsv_ps3_platform.h"')
replace("uchar.h", "#if TEAVM_PSP\n", "#if TEAVM_PSP || defined(TEAVM_PS3)\n")
replace("uchar.h", "#include <wchar.h>", "#include <wchar.h>\n#include <stdint.h>")
replace("file.c", "#if TEAVM_UNIX\n#include <unistd.h>", "#if TEAVM_UNIX || defined(TEAVM_PS3)\n#include <unistd.h>")
replace("time.c", "#if TEAVM_PSP\n", '#if TEAVM_PSP\n#include "stsv_ps3_platform.h"\n')
replace("time.c", "return (int64_t) sceKernelLibcTime(NULL) * 1000000000;", "return stsv_ps3_nanotime();")
replace("time.c", "return (int64_t) sceKernelLibcTime(NULL) * 1000;", "return stsv_wall_us() / 1000;")
replace("time.c", '#include "stsv_ps3_platform.h"', '#include "stsv_ps3_platform.h"\n#include "stsvita_glue.h"')
replace("file.c", "struct passwd *pw = getpwuid(getuid());", 'const char *home = "/dev_hdd0/game/STSP00001/USRDIR";')
replace("file.c", "teavm_mbToChar16(pw->pw_dir, &length)", "teavm_mbToChar16((char*) home, &length)")
replace("file.c", 'pathMax = pathconf(".", _PC_PATH_MAX);', "pathMax = 1024; /* PSL1GHT has no pathconf. */")
# The GC trusts its region and card tables to start zeroed, as mmap and
# VirtualAlloc guarantee. PSL1GHT malloc returns dirty memory, so the first
# full GC walked garbage region offsets and looped forever in defragment.
replace("memory.c", "void* teavm_virtualAlloc(int64_t size) {\n    return malloc(size);",
        "void* teavm_virtualAlloc(int64_t size) {\n    return calloc(1, (size_t) size); /* PS3: GC tables must start zeroed */")

steps = [
    ("arrayclass.c", fix.patch_arrayclass_align),
    ("core.h", fix.patch_array_data_offset),
    ("reflection.c", fix.patch_reflection_data_offset),
    ("classes/java/lang/Float.c", fix.patch_float_tostring),
    ("classes/org/teavm/runtime/GC.c", fix.patch_gc_timing),
    ("classes/com/megacrit/cardcrawl/screens/options/DropdownMenu.c", fix.patch_dropdown_guard),
    ("classes/com/megacrit/cardcrawl/helpers/controller/CInputHelper.c", fix.patch_controller_model_ps4),
    ("classes/com/badlogic/gdx/graphics/g2d/freetype/FreeTypeFontGenerator.c", fix.patch_freetype_font_generator),
]
for name, fn in steps:
    changed = fn(gen / name)
    print(f"[ps3-patch] {name}: {'patched' if changed else 'already patched'}")

# Static-root tables reserve a pointer-sized count slot. TeaVM's Java GC
# reads it as an int; on big-endian LP64 that reads the leading zero word.
# Keep both marking and pointer-relocation traversal consistent with memory.c.
gc_path = gen / "classes/org/teavm/runtime/GC.c"
gc_text = gc_path.read_text()
old = "teavm_local_1 = teavm_gc_staticRoots;\n    teavm_local_2 = (*(int32_t*) teavm_local_1);"
new = "teavm_local_1 = teavm_gc_staticRoots;\n    teavm_local_2 = (int32_t) (*(intptr_t*) teavm_local_1); /* PS3 BE64 root count */"
if gc_text.count(old) == 2:
    gc_path.write_text(gc_text.replace(old, new))
elif gc_text.count(new) != 2:
    raise SystemExit("GC.c: expected exactly two static-root count reads")
