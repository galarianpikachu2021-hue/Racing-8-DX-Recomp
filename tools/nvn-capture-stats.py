#!/usr/bin/env python3
"""Summarise nvn_capture_*.txt files written by the NVN tracer (KARTRECOMP_NVN_TRACE=1).

  python3 tools/nvn-capture-stats.py <dir-with-captures>

For each function: total calls in the captures, and the distinct values seen in
argument registers that are scalars for that function. This tells the native
backend which enum values (texture formats, primitive modes, index types, ...)
it really has to support. Register numbers follow AAPCS64: x0 is the object
(for example the command buffer), x1.. are the parameters.
"""
import collections
import glob
import os
import sys

# function -> list of (register index, label). Scalars worth enumerating.
WATCH = {
    "nvnTextureBuilderSetFormat": [(1, "format")],
    "nvnTextureBuilderSetTarget": [(1, "target")],
    "nvnTextureBuilderSetSamples": [(1, "samples")],
    "nvnTextureBuilderSetFlags": [(1, "flags")],
    "nvnTextureBuilderSetLevels": [(1, "levels")],
    "nvnTextureViewSetFormat": [(1, "format")],
    "nvnTextureViewSetTarget": [(1, "target")],
    "nvnCommandBufferDrawElements": [(1, "mode"), (2, "index_type")],
    "nvnCommandBufferDrawElementsInstanced": [(1, "mode"), (2, "index_type")],
    "nvnCommandBufferDrawElementsBaseVertex": [(1, "mode"), (2, "index_type")],
    "nvnCommandBufferDrawArrays": [(1, "mode")],
    "nvnCommandBufferDrawArraysInstanced": [(1, "mode")],
    "nvnCommandBufferBindProgram": [(2, "stage_bits")],
    "nvnCommandBufferBindUniformBuffer": [(1, "stage"), (2, "slot")],
    "nvnCommandBufferBindTexture": [(1, "stage"), (2, "slot")],
    "nvnCommandBufferSetRenderTargets": [(1, "color_count")],
    "nvnCommandBufferBarrier": [(1, "bits")],
    "nvnBlendStateSetBlendFunc": [(1, "src_rgb"), (2, "dst_rgb"), (3, "src_a"), (4, "dst_a")],
    "nvnBlendStateSetBlendEquation": [(1, "eq_rgb"), (2, "eq_a")],
    "nvnDepthStencilStateSetDepthFunc": [(1, "func")],
    "nvnPolygonStateSetCullFace": [(1, "cull")],
    "nvnPolygonStateSetFrontFace": [(1, "front")],
    "nvnPolygonStateSetPolygonMode": [(1, "mode")],
    "nvnSamplerBuilderSetMinMagFilter": [(1, "min"), (2, "mag")],
    "nvnSamplerBuilderSetWrapMode": [(1, "s"), (2, "t"), (3, "r")],
    "nvnSamplerBuilderSetMaxAnisotropy": [],
    "nvnMemoryPoolBuilderSetFlags": [(1, "flags")],
    "nvnVertexAttribStateSetFormat": [(1, "format")],
    "nvnMultisampleStateSetSamples": [(1, "samples")],
}

def main(path):
    calls = collections.Counter()
    values = collections.defaultdict(collections.Counter)
    frames = set()
    threads = collections.Counter()
    for f in sorted(glob.glob(os.path.join(path, "nvn_capture_*.txt"))):
        for line in open(f):
            if line.startswith("#"):
                continue
            head, _, _floats = line.partition(" | ")
            parts = head.split()
            if len(parts) < 11:
                continue
            frame, thread, name = parts[0], parts[1], parts[2]
            regs = [int(x, 16) for x in parts[3:11]]
            frames.add(frame)
            threads[thread] += 1
            calls[name] += 1
            for reg, label in WATCH.get(name, []):
                # Enum and count parameters are 32-bit; ignore the upper half.
                values[(name, label)][regs[reg] & 0xFFFFFFFF] += 1
    print(f"captured frames: {len(frames)}, calls: {sum(calls.values())}, "
          f"functions: {len(calls)}, recording threads: {len(threads)}")
    print("\n--- calls per function (captures only) ---")
    for name, n in calls.most_common():
        print(f"{n:9d}  {name}")
    print("\n--- distinct scalar argument values ---")
    for (name, label), counter in sorted(values.items()):
        shown = ", ".join(f"{v:#x}x{c}" for v, c in counter.most_common(16))
        more = "" if len(counter) <= 16 else f" (+{len(counter) - 16} more)"
        print(f"{name}.{label}: {shown}{more}")

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
