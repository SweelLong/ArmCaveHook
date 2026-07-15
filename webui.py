from __future__ import annotations

import io
import json
import re
import sys
from pathlib import Path

import lief
import markdown as md_lib
from werkzeug.utils import secure_filename
from flask import (
    Flask, Response, g, jsonify, render_template, request,
    send_file, stream_with_context,
)

PROJECT_ROOT = Path(__file__).resolve().parent
PLUGINS_DIR = PROJECT_ROOT / "plugins"
BINARIES_DIR = PROJECT_ROOT / "binaries"
DATA_DIR = PROJECT_ROOT / "data"
sys.path.insert(0, str(PROJECT_ROOT))

from tools.pipeline import run_pipeline, _seg
from tools.plugin import load_plugin
from tools.compiler import compile_plugin


STANDARD_API_RE = re.compile(r"\b(?:hook|cave|inject_hex|inject_asm)(?:_ex)?\s*\(|\binit\s*\(")


def _load_api_docs() -> dict:
    path = DATA_DIR / "api_docs.json"
    return json.loads(path.read_text(encoding="utf-8"))


def _uses_standard_api(source: str) -> bool:
    return bool(STANDARD_API_RE.search(source))


def _load_standard_blob(path: Path):
    try:
        return compile_plugin(path)
    except Exception:
        return None


def _resolved_action_segment(plugin_stem: str, index: int, action, prefix: str | None = None) -> str:
    if action.kind in ("hook", "pre_hook"):
        return _seg(plugin_stem, index, action, prefix)
    if action.kind == "cave":
        return _seg(plugin_stem, index, action, prefix)
    return "-"


def _segment_summary(plugin_stem: str, actions, prefix: str | None = None) -> str:
    if not actions:
        return "-"
    segments = []
    cave_index = 0
    for action in actions:
        if action.kind not in ("hook", "pre_hook", "cave"):
            continue
        seg = _resolved_action_segment(plugin_stem, cave_index, action, prefix)
        cave_index += 1
        if seg != "-" and seg not in segments:
            segments.append(seg)
    if not segments:
        return "-"
    if len(segments) <= 2:
        return ", ".join(segments)
    return f"{segments[0]}, {segments[1]} +{len(segments) - 2}"


app = Flask(__name__)

# ── i18n ────────────────────────────────────────────────

T = {
    "en": {
        "title": "ArmCaveHook",
        "overview": "Overview",
        "plugins": "Plugins",
        "pipeline": "Pipeline",
        "lang_switch": "中文",
        "plugin_manager": "Plugin Manager",
        "new_plugin": "+ New Plugin",
        "filename": "Filename",
        "source_code": "Source Code",
        "save": "Save",
        "cancel": "Cancel",
        "edit": "Edit",
        "delete": "Delete",
        "back": "Back",
        "hook_api": "Hook API",
        "show_api": "Show API",
        "hide_api": "Hide API",
        "segment": "Segment",
        "hook_addr": "Hook Addr",
        "hook_window": "Window",
        "actions": "Actions",
        "no_plugins": "No plugins yet — click \"+ New Plugin\" to create one.",
        "parse_error": "parse error",
        "pipeline_control": "Pipeline Control",
        "target_binary": "Target Binary",
        "output_name": "Output Name",
        "output_optional": "optional",
        "inject_btn": "Inject & Patch",
        "output_log": "Execution Log",
        "select_binary": "-- select binary --",
        "no_binaries": "No binaries found in project directory.",
        "confirm_delete": 'Delete plugin',
        "confirm_delete_msg": 'Are you sure?',
        "saved": "Plugin saved.",
        "deleted": "Plugin deleted.",
        "validation_error": "Validation Error",
        "binary_required": "Please select a target binary.",
        "file": "File",
        "size": "Size",
        "name": "Name",
        "va": "VA",
        "vsize": "VSize",
        "fsize": "FSize",
        "binary_info": "Binary Info",
        "segments_label": "Segments",
        "hex_preview": "Hex Preview",
        "view_btn": "View",
        "download_patched": "Download Patched Binary",
        "uploading": "Uploading",
        "uploaded": "Uploaded",
        "upload_failed": "Upload failed",
        "compiling": "Compiling...",
        "compile_ok": "Compilation OK",
        "compile_success_msg": "Compilation successful",
        "compile_failed": "Compilation failed",
        "compile_error": "Compile error",
        "network_error": "Network error",
        "compile_check_btn": "Compile Check",
        "download_btn": "Download",
        "filename_required": "Filename is required.",
        "no_code": "No code to compile",
        "failed_load_plugin": "Failed to load plugin",
        "failed_delete": "Failed to delete",
        "unknown_error": "Unknown error",
        "drag_text": "Drop binary file here",
        "drag_hint": "or click to browse — ELF / Mach-O supported",
        "format_label": "Format",
        "arch_label": "Arch",
        "entrypoint_label": "Entrypoint",
        "file_mgmt": "File Management",
        "delete_file": "Delete",
        "confirm_delete_file": "Delete this file?",
        "edit_plugin": "Edit Plugin",
        "enable": "Enable",
        "priority": "Priority",
        "validation_plugin_name_required": "plugin name is required",
        "validation_plugin_name_invalid": "invalid plugin filename",
        "validation_plugin_exists": "plugin '{name}' already exists",
        "validation_plugin_not_found": "plugin not found",
        "available_symbols": "Available Symbols",
        "reference_binary": "Reference Binary",
        "fuzzy_search": "Fuzzy search...",
        "no_binary_for_symbols": "Select a binary to see available symbols",
        "builtin": "built-in",
        "imported": "imported",
    },
    "zh": {
        "title": "ArmCaveHook",
        "overview": "总览",
        "plugins": "插件管理",
        "pipeline": "注入控制",
        "lang_switch": "English",
        "plugin_manager": "插件管理",
        "new_plugin": "+ 新建插件",
        "filename": "文件名",
        "source_code": "插件源码",
        "save": "保存",
        "cancel": "取消",
        "edit": "编辑",
        "delete": "删除",
        "back": "返回",
        "hook_api": "Hook API",
        "show_api": "显示 API",
        "hide_api": "隐藏 API",
        "segment": "段名",
        "hook_addr": "Hook 地址",
        "hook_window": "窗口",
        "actions": "操作",
        "no_plugins": '暂无插件 — 点击 "+ 新建插件" 创建一个。',
        "parse_error": "解析错误",
        "pipeline_control": "注入控制",
        "target_binary": "目标二进制",
        "output_name": "输出文件名",
        "output_optional": "可选",
        "inject_btn": "执行注入",
        "output_log": "执行日志",
        "select_binary": "-- 选择二进制文件 --",
        "no_binaries": "项目目录中未找到二进制文件。",
        "confirm_delete": "删除插件",
        "confirm_delete_msg": "确定要删除此插件吗？",
        "saved": "插件已保存。",
        "deleted": "插件已删除。",
        "validation_error": "校验失败",
        "binary_required": "请先选择目标二进制文件。",
        "file": "文件",
        "size": "大小",
        "name": "名称",
        "va": "虚拟地址",
        "vsize": "虚拟大小",
        "fsize": "文件大小",
        "binary_info": "二进制信息",
        "segments_label": "段信息",
        "hex_preview": "十六进制预览",
        "view_btn": "查看",
        "download_patched": "下载修补后的二进制",
        "uploading": "正在上传",
        "uploaded": "已上传",
        "upload_failed": "上传失败",
        "compiling": "编译中...",
        "compile_ok": "编译成功",
        "compile_success_msg": "编译成功",
        "compile_failed": "编译失败",
        "compile_error": "编译错误",
        "network_error": "网络错误",
        "compile_check_btn": "编译检查",
        "download_btn": "下载",
        "filename_required": "文件名不能为空。",
        "no_code": "没有可编译的代码",
        "failed_load_plugin": "加载插件失败",
        "failed_delete": "删除失败",
        "unknown_error": "未知错误",
        "drag_text": "拖拽二进制文件到此处",
        "drag_hint": "或点击浏览 — 支持 ELF / Mach-O 格式",
        "format_label": "格式",
        "arch_label": "架构",
        "entrypoint_label": "入口点",
        "file_mgmt": "文件管理",
        "delete_file": "删除",
        "confirm_delete_file": "确定要删除此文件吗？",
        "edit_plugin": "编辑插件",
        "enable": "启用",
        "priority": "优先级",
        "validation_plugin_name_required": "插件文件名不能为空",
        "validation_plugin_name_invalid": "无效的插件文件名",
        "validation_plugin_exists": "插件 '{name}' 已存在",
        "validation_plugin_not_found": "插件未找到",
        "available_symbols": "可用符号",
        "reference_binary": "参考二进制",
        "fuzzy_search": "模糊搜索...",
        "no_binary_for_symbols": "选择一个二进制文件以查看可用符号",
        "builtin": "内置",
        "imported": "导入",
    },
}


def t(key: str, lang: str | None = None) -> str:
    if lang is None:
        lang = g.get("lang", "zh")
    return T.get(lang, T["zh"]).get(key, T["zh"].get(key, key))


@app.before_request
def detect_lang():
    lang = request.args.get("lang") or request.cookies.get("lang") or "zh"
    if lang not in T:
        lang = "zh"
    g.lang = lang


@app.context_processor
def inject_i18n():
    return {"t": t, "lang": g.get("lang", "zh"), "plugins_count": len(_list_plugins()), "T_JSON": T}


# ── helpers ─────────────────────────────────────────────

def _read_readme_html() -> str:
    readme = PROJECT_ROOT / "README.md"
    try:
        text = readme.read_text(encoding="utf-8", errors="ignore")
        html = md_lib.markdown(text, extensions=["fenced_code", "tables"])
        html = re.sub(
            r'<li>\[([ x])\] ',
            lambda m: '<li class="task-list-item"><input type="checkbox" disabled' + (' checked' if m.group(1) == 'x' else '') + '> ',
            html,
        )
        return html
    except FileNotFoundError:
        return "<p><em>No README.md found.</em></p>"


def _list_binaries() -> list[dict]:
    binaries = []
    if BINARIES_DIR.exists():
        for f in BINARIES_DIR.iterdir():
            if f.is_file() and not f.name.startswith("."):
                try:
                    size = f.stat().st_size
                except OSError:
                    size = 0
                binaries.append({"name": f.name, "size": size, "path": str(f)})
    return sorted(binaries, key=lambda b: b["name"])


def _list_plugins() -> list[dict]:
    plugins = []
    if PLUGINS_DIR.exists():
        for path in sorted(p for p in PLUGINS_DIR.iterdir() if p.suffix == ".cpp"):
            try:
                spec = load_plugin(path)
                blob = _load_standard_blob(path)
                actions = blob.declarations if blob else None
                if actions:
                    spec.actions = actions
                segment = _segment_summary(path.stem, actions, blob.default_segment if blob else None)
                plugins.append({
                    "name": path.name,
                    "stem": path.stem,
                    "segment": segment,
                    "hook_addr": spec.summary(),
                    "hook_size": "auto",
                    "size": "auto",
                    "content": path.read_text(encoding="utf-8", errors="ignore"),
                    "api": "standard" if spec.actions else "empty",
                })
            except Exception:
                plugins.append({
                    "name": path.name,
                    "stem": path.stem,
                    "error": True,
                    "content": path.read_text(encoding="utf-8", errors="ignore"),
                })
    return plugins


def _validate_plugin_source(source: str) -> list[str]:
    return []


# ── routes ──────────────────────────────────────────────

@app.route("/")
def index():
    return render_template("index.html", readme_html=_read_readme_html())


@app.route("/plugins")
def plugins_page():
    return render_template("plugins.html", plugins=_list_plugins(), binaries=_list_binaries(), api_docs=_load_api_docs())


@app.route("/files")
def files_page():
    return render_template("files.html", binaries=_list_binaries())

@app.route("/pipeline")
def pipeline_page():
    return render_template("pipeline.html", binaries=_list_binaries(), plugins=_list_plugins())


@app.route("/api/docs", methods=["GET"])
def api_docs():
    return jsonify(_load_api_docs())


# ── API ─────────────────────────────────────────────────

@app.route("/api/plugins", methods=["GET"])
def api_plugins():
    return jsonify(_list_plugins())


@app.route("/api/plugins/<name>", methods=["GET"])
def api_plugin_get(name: str):
    path = PLUGINS_DIR / name
    if not path.exists():
        return jsonify({"error": "plugin not found"}), 404
    try:
        spec = load_plugin(path)
        blob = _load_standard_blob(path)
        actions = blob.declarations if blob else None
        if actions:
            spec.actions = actions
        segment = _segment_summary(path.stem, actions, blob.default_segment if blob else None)
        return jsonify({
            "name": path.name,
            "stem": path.stem,
            "content": path.read_text(encoding="utf-8", errors="ignore"),
            "segment": segment,
            "hook_addr": spec.summary(),
            "hook_size": "auto",
            "size": "auto",
            "api": "standard" if spec.actions else "empty",
        })
    except Exception as exc:
        return jsonify({"error": str(exc)}), 500


@app.route("/api/plugins", methods=["POST"])
def api_plugin_create():
    data = request.get_json(force=True)
    name = (data.get("name") or "").strip()
    content = (data.get("content") or "").strip()

    if not name:
        return jsonify({"error": t("validation_plugin_name_required")}), 400
    if "." not in name:
        name += ".cpp"
    if not re.match(r"^[A-Za-z_][A-Za-z0-9_]*\.cpp$", name):
        return jsonify({"error": t("validation_plugin_name_invalid")}), 400

    errors = _validate_plugin_source(content)
    if errors:
        return jsonify({"error": "\n".join(errors)}), 400

    path = PLUGINS_DIR / name
    if path.exists():
        return jsonify({"error": t("validation_plugin_exists").format(name=name)}), 409

    PLUGINS_DIR.mkdir(parents=True, exist_ok=True)
    path.write_text(content + "\n", encoding="utf-8")
    return jsonify({"ok": True, "name": name})


@app.route("/api/plugins/<name>", methods=["PUT"])
def api_plugin_update(name: str):
    data = request.get_json(force=True)
    content = (data.get("content") or "").strip()
    new_name = (data.get("name") or "").strip()

    path = PLUGINS_DIR / name
    if not path.exists():
        return jsonify({"error": t("validation_plugin_not_found")}), 404

    errors = _validate_plugin_source(content)
    if errors:
        return jsonify({"error": "\n".join(errors)}), 400

    if new_name and new_name != name:
        if "." not in new_name:
            new_name += ".cpp"
        if not re.match(r"^[A-Za-z_][A-Za-z0-9_]*\.cpp$", new_name):
            return jsonify({"error": t("validation_plugin_name_invalid")}), 400
        new_path = PLUGINS_DIR / new_name
        if new_path.exists():
            return jsonify({"error": t("validation_plugin_exists").format(name=new_name)}), 409
        path.rename(new_path)
        path = new_path

    path.write_text(content + "\n", encoding="utf-8")
    return jsonify({"ok": True})


@app.route("/api/plugins/<name>", methods=["DELETE"])
def api_plugin_delete(name: str):
    path = PLUGINS_DIR / name
    if not path.exists():
        return jsonify({"error": "plugin not found"}), 404
    path.unlink()
    return jsonify({"ok": True})


@app.route("/api/binaries", methods=["GET"])
def api_binaries():
    return jsonify(_list_binaries())


@app.route("/api/binaries/<name>", methods=["DELETE"])
def api_binary_delete(name: str):
    path = BINARIES_DIR / name
    if not path.exists():
        return jsonify({"error": "binary not found"}), 404
    path.unlink()
    return jsonify({"ok": True})


@app.route("/api/pipeline/run", methods=["POST"])
def api_pipeline_run():
    data = request.get_json(force=True)
    binary_name = (data.get("binary") or "").strip()
    output_name = (data.get("output") or "").strip()
    selected_plugins = data.get("plugins", None)
    if selected_plugins is not None and not isinstance(selected_plugins, list):
        selected_plugins = None

    if not binary_name:
        return jsonify({"error": "no binary selected"}), 400

    binary_path = BINARIES_DIR / binary_name
    if not binary_path.exists():
        return jsonify({"error": f"binary not found: {binary_name}"}), 404

    if not output_name:
        output_name = binary_path.stem + ".patched" + binary_path.suffix

    output_path = BINARIES_DIR / output_name

    def generate():
        buf = io.StringIO()

        class Tee:
            def write(self, s):
                buf.write(s)
                return len(s)

            def flush(self):
                pass

        old_stdout = sys.stdout
        sys.stdout = Tee()

        try:
            yield "data: ArmCaveHook pipeline starting...\n\n"
            run_pipeline(binary_path, output_path, PLUGINS_DIR, plugin_names=selected_plugins)
            sys.stdout = old_stdout
            for line in buf.getvalue().splitlines():
                if line.strip():
                    yield f"data: {line}\n\n"
            yield "data: \n\n"
            yield f"data: [Done] Output written to {output_path.name}\n\n"
            yield "data: __DONE__\n\n"
        except Exception as exc:
            sys.stdout = old_stdout
            import traceback
            traceback.print_exc()
            yield f"data: ERROR: {exc}\n\n"
            yield "data: __DONE__\n\n"

    return Response(
        stream_with_context(generate()),
        mimetype="text/event-stream",
        headers={"Cache-Control": "no-cache", "X-Accel-Buffering": "no"},
    )


@app.route("/api/upload", methods=["POST"])
def api_upload():
    f = request.files.get("file")
    if not f or not f.filename:
        return jsonify({"error": "no file provided"}), 400
    name = secure_filename(f.filename)
    if not name:
        return jsonify({"error": "invalid filename"}), 400
    BINARIES_DIR.mkdir(parents=True, exist_ok=True)
    dest = BINARIES_DIR / name
    f.save(str(dest))
    return jsonify({"ok": True, "name": name, "size": dest.stat().st_size})


@app.route("/api/binary/symbols", methods=["POST"])
def api_binary_symbols():
    """Return callable symbols (imports + builtins) from a binary."""
    data = request.get_json(force=True)
    name = (data.get("name") or "").strip()
    if not name:
        return jsonify({"error": "no binary name"}), 400
    path = BINARIES_DIR / name
    if not path.exists():
        return jsonify({"error": f"not found: {name}"}), 404

    try:
        from tools.symbols import list_available_symbols
        symbols = list_available_symbols(path)
        return jsonify({"symbols": symbols})
    except Exception as exc:
        return jsonify({"error": str(exc)}), 500


@app.route("/api/plugins/compile-check", methods=["POST"])
def api_compile_check():
    """Quick compile a C++ plugin snippet without running full pipeline."""
    import subprocess
    import tempfile

    data = request.get_json(force=True)
    source = (data.get("content") or "").strip()

    if not source:
        return jsonify({"error": "no source provided"}), 400

    with tempfile.TemporaryDirectory(prefix="armcave-check-") as td:
        src = Path(td) / "check.cpp"
        src.write_text(source, encoding="utf-8")
        try:
            from tools.compiler import compile_plugin
            blob = compile_plugin(src)
            actions = []
            cave_index = 0
            for a in blob.declarations:
                segment = _resolved_action_segment(src.stem, cave_index, a, blob.default_segment)
                if a.kind in ("hook", "pre_hook", "cave"):
                    cave_index += 1
                actions.append({
                    "kind": a.kind,
                    "address": "entry" if a.address is None else f"0x{a.address:x}",
                    "handler": a.handler,
                    "register_args": a.register_args or [],
                    "size": a.size,
                    "data": a.data,
                    "segment": segment,
                })
            return jsonify({
                "ok": True,
                "message": "Compilation successful",
                "segment_conflict": None,
                "actions": actions,
            })
        except subprocess.CalledProcessError as exc:
            return jsonify({"ok": False, "error": (exc.stderr or str(exc)).strip()}), 200
        except subprocess.TimeoutExpired:
            return jsonify({"ok": False, "error": "Compilation timed out"}), 200
        except FileNotFoundError:
            return jsonify({"ok": False, "error": "clang not found — install LLVM/Clang"}), 200


@app.route("/api/binary/info", methods=["POST"])
def api_binary_info():
    data = request.get_json(force=True)
    name = (data.get("name") or "").strip()
    if not name:
        return jsonify({"error": "no binary name"}), 400
    path = BINARIES_DIR / name
    if not path.exists():
        return jsonify({"error": f"not found: {name}"}), 404

    try:
        b = lief.parse(str(path))
        if b is None:
            return jsonify({"error": "failed to parse binary"}), 400

        is_macho = isinstance(b, lief.MachO.Binary)
        segments = []
        for seg in b.segments:
            segments.append({
                "name": seg.name,
                "va": f"0x{seg.virtual_address:x}",
                "vsize": f"0x{seg.virtual_size:x}",
                "fsize": f"0x{seg.file_size:x}",
                "prot": seg.max_protection if is_macho else seg.flags,
            })

        return jsonify({
            "format": "Mach-O" if is_macho else "ELF",
            "arch": str(b.header.cpu_type if is_macho else b.header.machine_type),
            "entrypoint": f"0x{b.entrypoint:x}" if b.entrypoint else "none",
            "segments": segments,
        })
    except Exception as exc:
        return jsonify({"error": str(exc)}), 500


@app.route("/api/binary/hex", methods=["POST"])
def api_binary_hex():
    data = request.get_json(force=True)
    name = (data.get("name") or "").strip()
    addr_str = (data.get("address") or "").strip()
    length = int(data.get("length", 32))

    if not name or not addr_str:
        return jsonify({"error": "name and address required"}), 400

    path = BINARIES_DIR / name
    if not path.exists():
        return jsonify({"error": f"not found: {name}"}), 404

    try:
        va = int(addr_str, 0)
        b = lief.parse(str(path))
        if b is None:
            return jsonify({"error": "failed to parse binary"}), 400

        raw = bytes(b.get_content_from_virtual_address(va, length))
        hex_str = raw.hex()
        # group by 4 bytes
        groups = [hex_str[i:i+8] for i in range(0, len(hex_str), 8)]
        ascii_str = "".join(chr(x) if 32 <= x < 127 else "." for x in raw)
        return jsonify({
            "address": f"0x{va:x}",
            "hex": " ".join(groups),
            "ascii": ascii_str,
            "bytes": list(raw),
        })
    except Exception as exc:
        return jsonify({"error": str(exc)}), 500


@app.route("/api/download/<name>")
def api_download(name: str):
    path = BINARIES_DIR / name
    if not path.exists():
        return jsonify({"error": "not found"}), 404
    return send_file(path, as_attachment=True, download_name=name)


if __name__ == "__main__":
    app.run(host="127.0.0.1", port=5000, debug=True)
