from __future__ import annotations

from pathlib import Path
import shutil

import lief

from .compiler import compile_plugin
from .plugin import load_plugin
from .segment import SegmentPlan, add_segment, seg_name, seg_va
from .patcher import build_hook_cave, patch_hook_window


def run_pipeline(input_path: Path, output_path: Path, plugins_dir: Path, dry_run: bool = False, plugin_names: list[str] | None = None) -> None:
    if not input_path.exists():
        raise FileNotFoundError(input_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    plugins = []
    if plugins_dir.exists():
        for path in sorted(plugins_dir.glob("*.c")):
            if plugin_names is not None and path.name not in plugin_names:
                continue
            plugins.append(load_plugin(path))

    if dry_run:
        for plugin in plugins:
            hook_addr = f"0x{plugin.hook_addr:x}" if plugin.hook_addr is not None else "entrypoint"
            print(
                f"{plugin.name}: segment={plugin.segment_core} "
                f"size=0x{plugin.size:x} hook={hook_addr} window=0x{plugin.hook_size:x}"
            )
        return

    if not plugins:
        raise RuntimeError("no plugins found")

    standalone = [p for p in plugins if p.hook_addr is None]
    hook_plugins = [p for p in plugins if p.hook_addr is not None]

    shutil.copyfile(input_path, output_path)

    def _reparse():
        b = lief.parse(str(output_path))
        if b is None:
            raise RuntimeError(f"failed to parse {output_path}")
        return b

    # ── standalone: each plugin gets its own segment ──
    for p in standalone:
        blob = compile_plugin(p.path)
        print(f"[standalone] {p.name}: {len(blob)} bytes -> segment {p.segment_core}")
        add_segment(output_path, SegmentPlan(p.segment_core, len(blob), blob), output_path)

    # ── hook: group by HOOK_ADDR, each group gets one cave ──
    if hook_plugins:
        from collections import defaultdict
        by_addr: dict[int, list] = defaultdict(list)
        for p in hook_plugins:
            by_addr[p.hook_addr].append(p)

        for hook_va, group in by_addr.items():
            p0 = group[0]
            seg = p0.segment_core
            hook_size = max(p.hook_size for p in group)

            binary = _reparse()
            original_insn = bytes(binary.get_content_from_virtual_address(hook_va, hook_size))
            print(f"[hook] 0x{hook_va:x} window={hook_size} bytes, segment={seg}")

            plugin_blobs = []
            for p in group:
                blob = compile_plugin(p.path)
                plugin_blobs.append(blob)
                print(f"  {p.name}: {len(blob)} bytes")

            control_overhead = 4 * len(plugin_blobs) + len(original_insn) + 4
            aligned_blobs_size = sum((len(b) + 3) & ~3 for b in plugin_blobs)
            cave_size = control_overhead + aligned_blobs_size

            add_segment(output_path, SegmentPlan(seg, cave_size, b""), output_path)
            binary = _reparse()
            cave_va = seg_va(binary, seg, cave_size)

            cave_blob = build_hook_cave(cave_va, hook_va, hook_size, original_insn, plugin_blobs)
            add_segment(output_path, SegmentPlan(seg, len(cave_blob), cave_blob), output_path)

            binary = _reparse()
            patch_hook_window(output_path, output_path, hook_va, hook_size, cave_va)
            print(f"[done] patched 0x{hook_va:x} -> 0x{cave_va:x}")
