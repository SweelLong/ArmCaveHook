from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class HookAction:
    kind: str
    address: int | None = None
    handler: str = ""
    segment: str = "auto"
    register_args: list[str] = field(default_factory=list)
    size: int = 0
    data: str = ""


@dataclass
class PluginSpec:
    path: Path
    name: str
    actions: list[HookAction] | None = None

    def summary(self) -> str:
        if self.actions:
            return ", ".join(f"{a.kind}:{'entry' if a.address is None else f'0x{a.address:x}'}" for a in self.actions)
        return "-"


def load_plugin(path: Path) -> PluginSpec:
    return PluginSpec(path=path, name=path.stem)
