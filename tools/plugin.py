from dataclasses import dataclass
from pathlib import Path
from typing import Optional
import re


DEFINE_RE = re.compile(r"^\s*#define\s+([A-Z0-9_]+)\s+(.+?)\s*$")


@dataclass
class PluginSpec:
    path: Path
    name: str
    defines: dict[str, str]

    @property
    def segment_core(self) -> str:
        raw = self.defines.get("SEGMENT_NAME", "nulcorepivot")
        return raw[2:] if raw.startswith("__") else raw[1:] if raw.startswith(".") else raw

    @property
    def segment_size_auto(self) -> bool:
        """True when SEGMENT_SIZE is NOT defined — pipeline will auto-calc."""
        return "SEGMENT_SIZE" not in self.defines

    @property
    def size(self) -> int | None:
        raw = self.defines.get("SEGMENT_SIZE")
        return int(raw, 0) if raw is not None else None

    @property
    def hook_file_off(self) -> int | None:
        raw = self.defines.get("HOOK_ADDR")
        return int(raw, 0) if raw is not None else None

    @property
    def hook_size(self) -> int:
        raw = self.defines.get("HOOK_SIZE", "0x4")
        return int(raw, 0)

    @property
    def detour(self) -> bool:
        raw = self.defines.get("HOOK_DETOUR")
        return raw is not None and int(raw, 0) != 0

    @property
    def register_args(self) -> Optional[list[str]]:
        raw = self.defines.get("REGISTER_ARGS")
        if raw is None:
            return None
        regs = [r.strip().lower() for r in raw.split(",")]
        for r in regs:
            if not r or (r[0] not in ("x", "w") or not r[1:].isdigit()):
                raise ValueError(f"Invalid register name in REGISTER_ARGS: {r}")
        return regs


def load_plugin(path: Path) -> PluginSpec:
    defines: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        m = DEFINE_RE.match(line)
        if m:
            defines[m.group(1)] = m.group(2).split("//", 1)[0].strip()
    return PluginSpec(path=path, name=path.stem, defines=defines)
