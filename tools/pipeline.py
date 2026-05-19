from __future__ import annotations

from pathlib import Path
import shutil

import lief

from .compiler import compile_plugin
from .plugin import load_plugin
from .segment import SegmentPlan, add_segment, seg_name, seg_va
from .patcher import build_hook_cave, patch_hook_window


def run_pipeline(input_path: Path, output_path: Path, plugins_dir: Path, dry_run: bool = False) -> None:
    if not input_path.exists():
        raise FileNotFoundError(input_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    plugins = []
    if plugins_dir.exists():
        for path in sorted(plugins_dir.glob("*.c")):
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

    segment_core = plugins[0].segment_core
    hook = plugins[0]
    hook_size = hook.hook_size

    shutil.copyfile(input_path, output_path)

    binary = lief.parse(str(output_path))
    if binary is None:
        raise RuntimeError(f"failed to parse {output_path}")

    hook_va = hook.hook_addr if hook.hook_addr is not None else binary.entrypoint
    original_insn = bytes(binary.get_content_from_virtual_address(hook_va, hook_size))
    plugin_blobs = [compile_plugin(p.path) for p in plugins]

    control_overhead = 4 * len(plugin_blobs) + len(original_insn) + 4
    aligned_blobs_size = sum((len(b) + 3) & ~3 for b in plugin_blobs)
    cave_size = control_overhead + aligned_blobs_size

    add_segment(output_path, SegmentPlan(segment_core, cave_size, b""), output_path)

    binary = lief.parse(str(output_path))
    if binary is None:
        raise RuntimeError(f"failed to parse {output_path}")

    cave_va = seg_va(binary, segment_core, cave_size)
    cave_blob = build_hook_cave(cave_va, hook_va, hook_size, original_insn, plugin_blobs)
    add_segment(output_path, SegmentPlan(segment_core, len(cave_blob), cave_blob), output_path)

    binary = lief.parse(str(output_path))
    if binary is None:
        raise RuntimeError(f"failed to parse {output_path}")

    patch_hook_window(output_path, output_path, hook_va, hook_size, cave_va)
