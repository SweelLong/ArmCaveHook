from __future__ import annotations

from pathlib import Path
import shutil
import subprocess

import lief

from .compiler import compile_plugin
from .plugin import load_plugin
from .segment import SegmentPlan, add_segment, seg_name, seg_va
from .patcher import build_hook_cave, patch_hook_macho, patch_hook_window


def run_pipeline(input_path: Path, output_path: Path, plugins_dir: Path, dry_run: bool = False, plugin_names: list[str] | None = None) -> None:
    if not input_path.exists():
        raise FileNotFoundError(input_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    plugins = []
    if plugins_dir.exists():
        if plugin_names is not None:
            for name in plugin_names:
                path = plugins_dir / name
                if path.exists():
                    plugins.append(load_plugin(path))
        else:
            for path in sorted(plugins_dir.glob("*.c")):
                plugins.append(load_plugin(path))

    if dry_run:
        for plugin in plugins:
            if plugin.hook_file_off is not None:
                hook_addr = f"0x{plugin.hook_file_off:x}"
                print(f"{plugin.name}: segment={plugin.segment_core} hook={hook_addr} window=0x{plugin.hook_size:x}")
            elif plugin.hook_nop_addrs:
                addrs = ", ".join(f"0x{a:x}" for a in plugin.hook_nop_addrs)
                print(f"{plugin.name}: NOP {addrs} size=0x{plugin.hook_size:x}")
            else:
                print(f"{plugin.name}: standalone segment={plugin.segment_core}")
        return

    if not plugins:
        raise RuntimeError("no plugins found")

    # ── validate: BRANCH_GOTO_*() requires HOOK_BRANCH_HOST 1 ──
    for p in plugins:
        src = p.path.read_text(encoding="utf-8", errors="ignore")
        for macro in ("BRANCH_GOTO_DST()", "BRANCH_GOTO_NEXT()", "BRANCH_GOTO_CONV()"):
            if macro in src and not p.hook_branch_host:
                raise ValueError(
                    f"{p.name}: {macro} requires #define HOOK_BRANCH_HOST 1"
                )

    standalone = [p for p in plugins if p.hook_file_off is None and not p.hook_nop_addrs]
    hook_plugins = [p for p in plugins if p.hook_file_off is not None]

    shutil.copyfile(input_path, output_path)

    def _reparse():
        b = lief.parse(str(output_path))
        if b is None:
            raise RuntimeError(f"failed to parse {output_path}")
        return b

    # ── NOP: direct instruction NOP ──
    NOP_BYTES = b"\x1f\x20\x03\xd5"
    for p in plugins:
        if not p.hook_nop_addrs:
            continue
        nop_size = p.hook_size
        for off in p.hook_nop_addrs:
            print(f"[nop] 0x{off:x} size={nop_size}")
            data = bytearray(output_path.read_bytes())
            if off + nop_size > len(data):
                raise ValueError(f"NOP file offset 0x{off:x} out of range")
            for pos in range(off, off + nop_size, 4):
                data[pos:pos+4] = NOP_BYTES
            output_path.write_bytes(data)

    # ── standalone: each plugin gets its own segment ──
    for p in standalone:
        blob = compile_plugin(p.path, target_binary=input_path)
        blob_bytes = blob.build(0, 0)  # standalone: offset 0 in segment
        print(f"[standalone] {p.name}: {len(blob_bytes)} bytes -> segment {p.segment_core}")
        add_segment(output_path, SegmentPlan(p.segment_core, len(blob_bytes), blob_bytes), output_path)

    is_macho = isinstance(_reparse(), lief.MachO.Binary)

    # ── hook: group by HOOK_ADDR, each group gets one cave ──
    if hook_plugins:
        # Expand multi-addr plugins into per-address entries
        hook_entries: list[tuple] = []
        for p in hook_plugins:
            binary = _reparse()
            for off in p.hook_file_offs:
                va = binary.offset_to_virtual_address(off)
                if isinstance(va, int) and va < 0:
                    raise ValueError(f"failed to map file offset 0x{off:x} to VA for {p.name}")
                hook_entries.append((p, off, va))

        from collections import defaultdict
        by_addr: dict[int, list] = defaultdict(list)
        for p, off, va in hook_entries:
            by_addr[va].append((p, off))

        for hook_va, group in by_addr.items():
            p0, off0 = group[0]
            seg = p0.segment_core
            hook_size = max(p0.hook_size for p0, _ in group)
            hook_off = off0
            detour = any(p0.detour for p0, _ in group)
            branch_host = any(p0.hook_branch_host for p0, _ in group)

            binary = _reparse()
            original_insn = bytes(binary.get_content_from_virtual_address(hook_va, hook_size))

            # collect NOP VAs for CONV scan (convert file offsets → VAs)
            nop_vas: list[int] = []
            for p, _ in group:
                for nop_off in p.hook_nop_addrs:
                    nop_va = binary.offset_to_virtual_address(nop_off)
                    if isinstance(nop_va, int) and nop_va >= 0:
                        nop_vas.append(nop_va)

            mode = "DETOUR" if detour else "INLINE"
            if branch_host:
                mode += " BRANCH_HOST"
            print(f"[hook] 0x{hook_va:x} (file off 0x{hook_off:x}) window={hook_size} bytes, segment={seg}, mode={mode}")

            plugin_blobs = []
            for p, off in group:
                blob = compile_plugin(p.path, target_binary=input_path)
                blob.register_args = p.register_args
                plugin_blobs.append(blob)
                print(f"  {p.name}: {blob.total_bytes} bytes")

            if is_macho:
                hook_va, cave_va = patch_hook_macho(
                    output_path, output_path, hook_off, hook_size,
                    original_insn, plugin_blobs, seg_name=seg, detour=detour,
                    branch_host=branch_host, nop_addrs=nop_vas,
                )
            else:
                from .patcher import _patched_size
                if detour:
                    control_overhead = 4 + 4 * len(plugin_blobs) + 4 + 4
                else:
                    control_overhead = 4 + 4 * len(plugin_blobs) + 4 + _patched_size(original_insn) + 4
                from .compiler import PluginBlob
                aligned_blobs_size = sum((len(b) + 3) & ~3 for b in plugin_blobs)
                wrapper_overhead = sum(
                    4 * len(b.register_args) + 4
                    for b in plugin_blobs
                    if isinstance(b, PluginBlob) and b.register_args
                )
                cave_size = control_overhead + aligned_blobs_size + wrapper_overhead

                add_segment(output_path, SegmentPlan(seg, cave_size, b""), output_path)
                binary = _reparse()
                cave_va = seg_va(binary, seg, cave_size)

                cave_blob = build_hook_cave(cave_va, hook_va, hook_size, original_insn, plugin_blobs, detour=detour, branch_host=branch_host, nop_addrs=nop_vas)
                add_segment(output_path, SegmentPlan(seg, len(cave_blob), cave_blob), output_path)

                binary = _reparse()
                patch_hook_window(output_path, output_path, hook_va, hook_size, cave_va)
            print(f"[done] patched 0x{hook_va:x} -> 0x{cave_va:x}")

    if is_macho:
        subprocess.run(
            ["codesign", "--force", "--sign", "-", str(output_path)],
            capture_output=True,
        )
