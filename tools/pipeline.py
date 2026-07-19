from __future__ import annotations

from collections import defaultdict
from pathlib import Path
import hashlib
import re
import shutil
import subprocess

import lief

from .compiler import assemble_aarch64, compile_plugin
from .patcher import build_hook_cave, patch_bytes_va, patch_hook_macho, patch_hook_window
from .plugin import HookAction, load_plugin
from .segment import SegmentPlan, add_segment, seg_va, segment_file_offset, write_at_offset


def _parse(path: Path):
    binary = lief.parse(str(path))
    if binary is None:
        raise RuntimeError(f"failed to parse {path}")
    return binary


def _off(binary, va: int) -> int:
    off = binary.virtual_address_to_offset(va)
    if not isinstance(off, int) or off < 0:
        raise ValueError(f"cannot map VA 0x{va:x}")
    return off


def _entry(binary) -> int:
    ep = getattr(binary, "entrypoint", 0) or 0
    if not ep:
        raise ValueError("binary entrypoint not found")
    return ep


def _seg(plugin: str, index: int, action: HookAction, prefix: str | None = None) -> str:
    if action.segment and action.segment not in ("auto", "armcave"):
        return action.segment
    if prefix:
        raw_prefix = re.sub(r"[^A-Za-z0-9_]+", "_", prefix).strip("_") or "armcave"
        suffix = str(index)
        return f"{raw_prefix[:max(1, 14 - len(suffix))]}{suffix}"
    raw = f"{plugin}_{action.handler or action.kind}"
    base = re.sub(r"[^A-Za-z0-9_]+", "_", raw).strip("_").lower() or "armcave"
    suffix = hashlib.sha1(f"{plugin}:{index}:{action.kind}:{action.address}".encode()).hexdigest()[:3]
    return f"ac_{base[:7]}_{suffix}"


def _patch_payload(action: HookAction) -> bytes:
    if action.kind in ("hex", "bytes"):
        return bytes.fromhex(action.data)
    if action.kind == "asm":
        payload = assemble_aarch64(action.data.replace(";", "\n"))
        if action.size:
            if action.size % len(payload):
                raise ValueError("ASM size must be a multiple of assembled payload size")
            payload *= action.size // len(payload)
        return payload
    raise ValueError(f"unsupported patch action: {action.kind}")


def _clear_imm12(output_path: Path, action: HookAction) -> bool:
    if action.address is None:
        raise ValueError("clear_imm12 missing address")
    expected = int(action.data, 0)
    binary = _parse(output_path)
    off = _off(binary, action.address)
    data = bytearray(output_path.read_bytes())
    if off + 4 > len(data):
        raise ValueError(f"patch out of range: 0x{action.address:x}")
    current = int.from_bytes(data[off:off + 4], "little")
    if current != expected:
        print(f"[clear_imm12:skip] 0x{action.address:x} current=0x{current:08x} expected=0x{expected:08x}")
        return False
    data[off:off + 4] = (expected & ~0x003FFC00).to_bytes(4, "little")
    output_path.write_bytes(data)
    return True


def _standard(input_path: Path, output_path: Path, plugins: list, dry_run: bool) -> bool:
    compiled = []
    for spec in plugins:
        blob = compile_plugin(spec.path, target_binary=input_path)
        spec.actions = blob.declarations
        if blob.declarations:
            compiled.append((spec, blob))
    if not compiled:
        return False
    binary = _parse(output_path if output_path.exists() else input_path)
    direct = []
    hooks = defaultdict(list)
    pre_hooks = defaultdict(list)
    for spec, blob in compiled:
        cave_index = 0
        for i, action in enumerate(blob.declarations):
            addr = _entry(binary) if action.address is None and action.handler else action.address
            if addr is None:
                raise ValueError(f"{spec.name}: {action.kind} missing address")
            action.address = addr
            if action.kind in ("hex", "bytes", "asm", "clear_imm12"):
                direct.append(action)
            elif action.kind == "hook":
                action.segment = _seg(spec.name, cave_index, action, blob.default_segment)
                cave_index += 1
                hooks[addr].append((spec, blob, action))
            elif action.kind == "pre_hook":
                action.segment = _seg(spec.name, cave_index, action, blob.default_segment)
                cave_index += 1
                pre_hooks[addr].append((spec, blob, action))
            else:
                raise ValueError(f"{spec.name}: unsupported action {action.kind}")
    if dry_run:
        for actions in hooks.values():
            for _, _, a in actions:
                print(f"{a.kind}: 0x{a.address:x} segment={a.segment} handler={a.handler}")
        for actions in pre_hooks.values():
            for _, _, a in actions:
                print(f"{a.kind}: 0x{a.address:x} segment={a.segment} handler={a.handler}")
        for a in direct:
            print(f"{a.kind}: 0x{a.address:x} size={a.size or 'auto'}")
        return True
    for action in direct:
        if action.kind == "clear_imm12":
            changed = _clear_imm12(output_path, action)
            print(f"[clear_imm12] 0x{action.address:x} changed={int(changed)}")
            continue
        payload = _patch_payload(action)
        print(f"[{action.kind}] 0x{action.address:x} size={len(payload)}")
        patch_bytes_va(output_path, output_path, action.address, payload)
    for hook_va, group in pre_hooks.items():
        binary = _parse(output_path)
        hook_size = 4
        original = bytes(binary.get_content_from_virtual_address(hook_va, hook_size))
        if len(original) != hook_size:
            raise ValueError(f"cannot read hook window at 0x{hook_va:x}")
        seg = group[0][2].segment
        blobs = [blob.for_action(a) for _, blob, a in group]
        print(f"[pre_hook] 0x{hook_va:x} segment={seg} handlers={len(blobs)}")
        if isinstance(binary, lief.MachO.Binary):
            patch_hook_macho(output_path, output_path, _off(binary, hook_va), hook_size, original, blobs, seg, False, False)
        else:
            control = 4 + len(blobs) * 4 + 4 + len(original) + 4
            size = control + sum(b.total_bytes + (4 * len(b.register_args) + 4 if b.register_args else 0) for b in blobs)
            add_segment(output_path, SegmentPlan(seg, size, b""), output_path)
            binary = _parse(output_path)
            cave_va = seg_va(binary, seg, size)
            cave = build_hook_cave(cave_va, hook_va, hook_size, original, blobs, input_path, False, False)
            add_segment(output_path, SegmentPlan(seg, len(cave), cave), output_path)
            patch_hook_window(output_path, output_path, hook_va, hook_size, cave_va)
        print(f"[done] 0x{hook_va:x}")
    for hook_va, group in hooks.items():
        binary = _parse(output_path)
        hook_size = 4
        original = bytes(binary.get_content_from_virtual_address(hook_va, hook_size))
        if len(original) != hook_size:
            raise ValueError(f"cannot read hook window at 0x{hook_va:x}")
        detour = True
        branch = False
        seg = group[0][2].segment
        blobs = [blob.for_action(a) for _, blob, a in group]
        print(f"[hook] 0x{hook_va:x} segment={seg} handlers={len(blobs)}")
        if isinstance(binary, lief.MachO.Binary):
            patch_hook_macho(output_path, output_path, _off(binary, hook_va), hook_size, original, blobs, seg, detour, branch)
        else:
            control = 4 + len(blobs) * 4 + 4 + (4 if detour else len(original) + 4)
            size = control + sum(b.total_bytes + (4 * len(b.register_args) + 4 if b.register_args else 0) for b in blobs)
            add_segment(output_path, SegmentPlan(seg, size, b""), output_path)
            binary = _parse(output_path)
            cave_va = seg_va(binary, seg, size)
            cave = build_hook_cave(cave_va, hook_va, hook_size, original, blobs, input_path, detour, branch)
            add_segment(output_path, SegmentPlan(seg, len(cave), cave), output_path)
            patch_hook_window(output_path, output_path, hook_va, hook_size, cave_va)
        print(f"[done] 0x{hook_va:x}")
    return True


def run_pipeline(input_path: Path, output_path: Path, plugins_dir: Path, dry_run: bool = False, plugin_names: list[str] | None = None, whitelist: str | None = None, blacklist: str | None = None) -> None:
    if not input_path.exists():
        raise FileNotFoundError(input_path)
    plugins = []
    if plugins_dir.exists():
        names = plugin_names if plugin_names is not None else [p.name for p in sorted(plugins_dir.iterdir()) if p.suffix == ".cpp"]
        if whitelist:
            wl = set(whitelist.split(","))
            names = [n for n in names if n in wl]
        elif blacklist:
            bl = set(blacklist.split(","))
            names = [n for n in names if n not in bl]
        for name in names:
            path = plugins_dir / name
            if path.exists():
                plugins.append(load_plugin(path))
    if not plugins:
        raise RuntimeError("no plugins found")
    if not dry_run:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(input_path, output_path)
    if not _standard(input_path, output_path, plugins, dry_run):
        raise RuntimeError("no armcave actions found")
    if not dry_run and isinstance(_parse(output_path), lief.MachO.Binary):
        subprocess.run(["codesign", "--force", "--sign", "-", str(output_path)], capture_output=True)
