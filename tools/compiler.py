from __future__ import annotations

from pathlib import Path
import subprocess
import tempfile

import lief


def compile_plugin(path: Path) -> bytes:
    with tempfile.TemporaryDirectory(prefix="armcave-") as td:
        out = Path(td) / f"{path.stem}.o"
        cmd = [
            "clang",
            "-target",
            "arm64-apple-macosx13.0",
            "-c",
            "-Oz",
            "-ffreestanding",
            "-fno-stack-protector",
            "-fno-builtin",
            str(path),
            "-o",
            str(out),
        ]
        subprocess.run(cmd, check=True)
        obj = lief.parse(str(out))
        if obj is None:
            raise RuntimeError(f"failed to parse object file for {path}")
        text = next((s for s in obj.sections if s.name == "__text"), None)
        if text is None:
            raise RuntimeError(f"missing __text in {path}")
        return bytes(text.content)
