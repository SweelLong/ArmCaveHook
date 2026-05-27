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
            hook_addr = f"0x{plugin.hook_file_off:x}" if plugin.hook_file_off is not None else "entrypoint"
            size_str = f"0x{plugin.size:x}" if plugin.size else "auto"
            print(
                f"{plugin.name}: segment={plugin.segment_core} "
                f"size={size_str} hook={hook_addr} window=0x{plugin.hook_size:x}"
            )
        return

    if not plugins:
        raise RuntimeError("no plugins found")

    standalone = [p for p in plugins if p.hook_file_off is None]
    hook_plugins = [p for p in plugins if p.hook_file_off is not None]

    shutil.copyfile(input_path, output_path)

    def _reparse():
        b = lief.parse(str(output_path))
        if b is None:
            raise RuntimeError(f"failed to parse {output_path}")
        return b

    # convert file offset to VA
    for p in hook_plugins:
        binary = _reparse()
        va = binary.offset_to_virtual_address(p.hook_file_off)
        if isinstance(va, int) and va < 0:
            raise ValueError(f"failed to map file offset 0x{p.hook_file_off:x} to VA for {p.name}")
        p._hook_va = va

    # ── standalone: each plugin gets its own segment ──
    for p in standalone:
        blob = compile_plugin(p.path, target_binary=input_path)
        blob_bytes = blob.build(0, 0)  # standalone: offset 0 in segment
        print(f"[standalone] {p.name}: {len(blob_bytes)} bytes -> segment {p.segment_core}")
        add_segment(output_path, SegmentPlan(p.segment_core, len(blob_bytes), blob_bytes), output_path)

    is_macho = isinstance(_reparse(), lief.MachO.Binary)

    # ── hook: group by HOOK_ADDR, each group gets one cave ──
    if hook_plugins:
        from collections import defaultdict
        by_addr: dict[int, list] = defaultdict(list)
        for p in hook_plugins:
            by_addr[p._hook_va].append(p)

        for hook_va, group in by_addr.items():
            p0 = group[0]
            seg = p0.segment_core
            hook_size = max(p.hook_size for p in group)
            hook_off = p0.hook_file_off
            detour = any(p.detour for p in group)

            binary = _reparse()
            original_insn = bytes(binary.get_content_from_virtual_address(hook_va, hook_size))
            mode = "DETOUR" if detour else "INLINE"
            print(f"[hook] 0x{hook_va:x} (file off 0x{hook_off:x}) window={hook_size} bytes, segment={seg}, mode={mode}")

            plugin_blobs = []
            for p in group:
                blob = compile_plugin(p.path, target_binary=input_path)
                blob.register_args = p.register_args
                plugin_blobs.append(blob)
                print(f"  {p.name}: {blob.total_bytes} bytes")

            if is_macho:
                hook_va, cave_va = patch_hook_macho(
                    output_path, output_path, hook_off, hook_size,
                    original_insn, plugin_blobs, seg_name=seg, detour=detour,
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

                cave_blob = build_hook_cave(cave_va, hook_va, hook_size, original_insn, plugin_blobs, detour=detour)
                add_segment(output_path, SegmentPlan(seg, len(cave_blob), cave_blob), output_path)

                binary = _reparse()
                patch_hook_window(output_path, output_path, hook_va, hook_size, cave_va)
            print(f"[done] patched 0x{hook_va:x} -> 0x{cave_va:x}")

    if is_macho:
        subprocess.run(
            ["codesign", "--force", "--sign", "-", str(output_path)],
            capture_output=True,
        )
