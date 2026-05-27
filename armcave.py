#!/usr/bin/env python3
import argparse
from pathlib import Path

from tools.pipeline import run_pipeline


def main() -> int:
    parser = argparse.ArgumentParser(description="ArmCaveHook static patch pipeline")
    parser.add_argument("input", help="input Mach-O or ELF binary")
    parser.add_argument("-o", "--output", help="output binary path")
    parser.add_argument("--plugins", default="plugins", help="plugins directory")
    parser.add_argument("--dry-run", action="store_true", help="scan and plan only")
    args = parser.parse_args()

    import traceback
    try:
        inp = Path(args.input)
        out = Path(args.output) if args.output else inp.with_suffix(inp.suffix + ".patched")
        run_pipeline(inp, out, Path(args.plugins), dry_run=args.dry_run)
    except Exception:
        traceback.print_exc()
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
