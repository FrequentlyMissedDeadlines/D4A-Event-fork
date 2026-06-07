# pyright: reportUndefinedVariable=false

"""
Generate debug-friendly firmware and filesystem build metadata.

This script keeps firmware and filesystem identities separate because `upload`
and `uploadfs` can happen from different source states.

Outputs:
- include/generated/BuildInfo.h     (for firmware builds)
- data/www/buildinfo.html           (human-readable debug page)
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
from datetime import datetime, timezone

Import("env")  # noqa: F821  (PlatformIO SCons environment)
PIO_ENV = env  # noqa: F821

PROJECT_DIR = Path(env["PROJECT_DIR"]).resolve()  # noqa: F821
DATA_DIR = PROJECT_DIR / "data"
DATA_WWW_DIR = DATA_DIR / "www"
GENERATED_INCLUDE_DIR = PROJECT_DIR / "include" / "generated"
GENERATED_HEADER_PATH = GENERATED_INCLUDE_DIR / "BuildInfo.h"
BUILDINFO_HTML_PATH = DATA_WWW_DIR / "buildinfo.html"
SCHEMA_VERSION = 1
GENERATOR_VERSION = "1.0"
BUILDINFO_API_PATH = "/buildinfo.json"
IGNORED_CODE_DIRS = {
    ".git",
    ".pio",
    "venv",
    "scripts/testvenv",
    "include/generated",
    "tmp",
}
IGNORED_DATA_SUFFIXES = {".gz"}
IGNORED_DATA_FILES = {
    "www/buildinfo.html",
}


def utc_now_iso() -> str:
    """Return the current UTC timestamp in ISO-8601 format."""
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def run_git(*args: str) -> str:
    """Run a git command in the project root and return trimmed stdout."""
    result = subprocess.run(
        ["git", "-C", str(PROJECT_DIR), *args],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return ""
    return result.stdout.strip()


def get_git_metadata() -> dict:
    """Collect repository metadata with safe fallbacks when git is unavailable."""
    commit_sha = run_git("rev-parse", "HEAD")
    commit_short = run_git("rev-parse", "--short", "HEAD")
    branch = run_git("branch", "--show-current")
    exact_tag = run_git("describe", "--tags", "--exact-match")
    nearest_tag = run_git("describe", "--tags", "--abbrev=0")
    dirty = bool(run_git("status", "--porcelain", "--untracked-files=no"))

    return {
        "repository_name": PROJECT_DIR.name,
        "branch": branch or "unknown",
        "commit_sha": commit_sha or "unknown",
        "commit_short": commit_short or "unknown",
        "exact_tag": exact_tag,
        "nearest_tag": nearest_tag,
        "dirty": dirty,
    }


def iter_project_files(base_dirs: list[Path], ignored_files: set[str] | None = None, ignored_suffixes: set[str] | None = None):
    """Yield project files in a deterministic order for hashing and statistics."""
    ignored_files = ignored_files or set()
    ignored_suffixes = ignored_suffixes or set()

    for base_dir in base_dirs:
        if not base_dir.exists():
            continue
        if base_dir.is_file():
            rel_path = base_dir.relative_to(PROJECT_DIR).as_posix()
            if rel_path not in ignored_files and base_dir.suffix not in ignored_suffixes:
                yield base_dir
            continue

        for root, dirnames, filenames in os.walk(base_dir):
            root_path = Path(root)
            rel_root = root_path.relative_to(PROJECT_DIR).as_posix()
            dirnames[:] = sorted(
                d for d in dirnames if (f"{rel_root}/{d}" if rel_root != "." else d) not in IGNORED_CODE_DIRS
            )
            for filename in sorted(filenames):
                file_path = root_path / filename
                rel_path = file_path.relative_to(PROJECT_DIR).as_posix()
                if rel_path in ignored_files:
                    continue
                if file_path.suffix in ignored_suffixes:
                    continue
                yield file_path


def sha1_for_files(files) -> str:
    """Return a SHA-1 over file paths and contents."""
    digest = hashlib.sha1()
    for file_path in files:
        rel_path = file_path.relative_to(PROJECT_DIR).as_posix()
        digest.update(rel_path.encode("utf-8"))
        digest.update(b"\0")
        digest.update(file_path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def file_stats(files) -> dict:
    """Compute file count and total bytes for an iterable of paths."""
    file_list = list(files)
    return {
        "file_count": len(file_list),
        "total_bytes": sum(path.stat().st_size for path in file_list),
        "paths": [path.relative_to(PROJECT_DIR).as_posix() for path in file_list],
    }


def make_code_snapshot(git_meta: dict) -> dict:
    """Create a source snapshot for firmware-related files."""
    code_files = list(
        iter_project_files(
            [PROJECT_DIR / "src", PROJECT_DIR / "include", PROJECT_DIR / "platformio.ini", PROJECT_DIR / "scripts" / "generate_build_info.py"],
            ignored_files=set(),
            ignored_suffixes=set(),
        )
    )
    stats = file_stats(code_files)
    return {
        "built_at_utc": utc_now_iso(),
        "git": {
            "repository_name": git_meta["repository_name"],
            "branch": git_meta["branch"],
            "commit_sha": git_meta["commit_sha"],
            "commit_short": git_meta["commit_short"],
            "exact_tag": git_meta["exact_tag"],
            "nearest_tag": git_meta["nearest_tag"],
            "dirty": git_meta["dirty"],
        },
        "code_tree_hash": sha1_for_files(code_files),
        "file_count": stats["file_count"],
        "total_bytes": stats["total_bytes"],
    }


def make_filesystem_snapshot(git_meta: dict) -> dict:
    """Create a snapshot for the filesystem payload uploaded with `uploadfs`."""
    data_files = list(
        iter_project_files(
            [DATA_DIR],
            ignored_files=IGNORED_DATA_FILES,
            ignored_suffixes=IGNORED_DATA_SUFFIXES,
        )
    )
    stats = file_stats(data_files)
    return {
        "built_at_utc": utc_now_iso(),
        "data_tree_hash": sha1_for_files(data_files),
        "file_count": stats["file_count"],
        "total_bytes": stats["total_bytes"],
        "source_snapshot": {
            "repository_name": git_meta["repository_name"],
            "branch": git_meta["branch"],
            "commit_sha": git_meta["commit_sha"],
            "commit_short": git_meta["commit_short"],
            "exact_tag": git_meta["exact_tag"],
            "nearest_tag": git_meta["nearest_tag"],
            "dirty": git_meta["dirty"],
        },
    }


def cpp_string_literal(value: str) -> str:
    """Escape a Python string as a C++ string literal."""
    return json.dumps(value)


def bool_literal(value: bool) -> str:
    """Return a lowercase C++ boolean literal."""
    return "true" if value else "false"


def render_header(metadata: dict) -> str:
    """Render the generated firmware metadata header."""
    git_meta = metadata["git"]
    return f"""/**
 * @file BuildInfo.h
 * @brief Generated firmware build metadata.
 *
 * Auto-generated by scripts/generate_build_info.py.
 * Do not edit manually.
 */
#pragma once

#include <cstdint>
#include <pgmspace.h>

namespace BuildInfoConsts
{{
    constexpr uint32_t schema_version                = {SCHEMA_VERSION};
    constexpr bool     git_dirty                     = {bool_literal(git_meta['dirty'])};
    constexpr const char str_build_role[]        PROGMEM = \"running-firmware\";
    constexpr const char str_generator_version[] PROGMEM = {cpp_string_literal(GENERATOR_VERSION)};
    constexpr const char str_project_name[]      PROGMEM = {cpp_string_literal(metadata['project_name'])};
    constexpr const char str_pio_env[]           PROGMEM = {cpp_string_literal(metadata['pio_env'])};
    constexpr const char str_board[]             PROGMEM = {cpp_string_literal(metadata['board'])};
    constexpr const char str_built_at_utc[]      PROGMEM = {cpp_string_literal(metadata['built_at_utc'])};
    constexpr const char str_repository_name[]   PROGMEM = {cpp_string_literal(git_meta['repository_name'])};
    constexpr const char str_git_branch[]        PROGMEM = {cpp_string_literal(git_meta['branch'])};
    constexpr const char str_git_commit_sha[]    PROGMEM = {cpp_string_literal(git_meta['commit_sha'])};
    constexpr const char str_git_commit_short[]  PROGMEM = {cpp_string_literal(git_meta['commit_short'])};
    constexpr const char str_git_exact_tag[]     PROGMEM = {cpp_string_literal(git_meta['exact_tag'])};
    constexpr const char str_git_nearest_tag[]   PROGMEM = {cpp_string_literal(git_meta['nearest_tag'])};
    constexpr const char str_code_tree_hash[]    PROGMEM = {cpp_string_literal(metadata['code_tree_hash'])};
}}
"""


HTML_TEMPLATE = """<!DOCTYPE html>
<html>
<head>
  <meta charset=\"UTF-8\">
  <meta http-equiv=\"Cache-Control\" content=\"no-store\">
  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">
  <title>K10 Build Information</title>
  <link rel=\"stylesheet\" href=\"./amaker.css\">
  <style>
    .mono{font-family:Consolas,'Courier New',monospace;white-space:pre-wrap;word-break:break-word}
    .box{background:rgba(255,255,255,.58);padding:10px 12px;margin:10px 0;border-radius:6px}
  </style>
</head>
<body>
  <nav class=\"topnav\">
    <a href=\"./index.html\"> 🛜 Wifi</a>
    <a href=\"./camera.html\">📷 Live Cam</a>
    <a href=\"./BotScript.html\">🎮 JS Play</a>
    <a href=\"./buildinfo.html\" class=\"active\">🛠 Build infos</a>
  </nav>
  <h1 id=\"pageTitle\">🛠 Build infos info <span id=\"titleStatus\" style=\"font-size:.6em;color:#ddd\">[loading]</span></h1>
    <div class=\"box\" id=\"summary\">Loading firmware and filesystem identities…</div>
  <div class=\"box\"><strong>Running firmware</strong><pre id=\"fw\" class=\"mono\">Loading…</pre></div>
    <div class=\"box\"><strong>Mounted filesystem package</strong><pre id=\"fs\" class=\"mono\">Loading…</pre></div>
  <script>
        const api='__API_PATH__', fsMeta=__FILESYSTEM_META__, $=id=>document.getElementById(id);
        const fetchJson=u=>fetch(u+'?t='+Date.now(),{cache:'no-store'}).then(r=>{if(!r.ok)throw new Error(u+' -> HTTP '+r.status);return r.json();});
    const pretty=o=>JSON.stringify(o,null,2);
    const same=(a,b)=>a&&b&&a===b;
        Promise.all([fetchJson(api)]).then(([fwData])=>{
            const fw=fwData.firmware||{}, fg=fw.git||{};
            const sameCommit=same(fg.commit_sha,fsMeta.commit_sha), sameCode=same(fw.code_tree_hash,fsMeta.code_tree_hash), dirty=!!fg.dirty||!!fsMeta.dirty;
      $('summary').textContent = sameCommit&&sameCode&&!dirty
        ? 'Firmware and filesystem look aligned.'
        : 'Firmware and filesystem differ. This usually means upload and uploadfs were done from different source states.';
      $('fw').textContent = pretty(fwData);
            $('fs').textContent = pretty(fsMeta);
      $('titleStatus').textContent='[ready]';
    }).catch(err=>{
      $('summary').textContent='Unable to load build metadata: '+(err.message||err);
      $('fw').textContent='Unavailable';
      $('fs').textContent='Unavailable';
      $('titleStatus').textContent='[error]';
    });
  </script>
  <div class=\"statusbar\"><span class=\"status-left\">Firmware vs filesystem identity</span><span class=\"status-right\">ℹ️ Debug</span></div>
</body>
</html>
"""


def render_buildinfo_html(filesystem_meta: dict) -> str:
    """Render the static build info page."""
    return (HTML_TEMPLATE
            .replace("__API_PATH__", BUILDINFO_API_PATH)
            .replace("__FILESYSTEM_META__", json.dumps(filesystem_meta, separators=(",", ":"))))


def write_text_if_changed(path: Path, content: str) -> None:
    """Write text only when content changed to reduce rebuild noise."""
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    path.write_text(content, encoding="utf-8")


def generate_firmware_metadata(source=None, target=None, env=None, **_kwargs):  # noqa: ANN001
    """Generate the header embedded into the firmware binary."""
    env = env or PIO_ENV
    git_meta = get_git_metadata()
    metadata = make_code_snapshot(git_meta)
    metadata.update(
        {
            "project_name": PROJECT_DIR.name,
            "pio_env": env.subst("$PIOENV"),
            "board": env.subst("$BOARD"),
        }
    )
    header = render_header(metadata)
    write_text_if_changed(GENERATED_HEADER_PATH, header)
    print(f"buildinfo: wrote firmware header -> {GENERATED_HEADER_PATH.relative_to(PROJECT_DIR)}")


def generate_filesystem_metadata(source=None, target=None, env=None, **_kwargs):  # noqa: ANN001
    """Generate the filesystem HTML asset."""
    env = env or PIO_ENV
    git_meta = get_git_metadata()
    code_snapshot = make_code_snapshot(git_meta)
    filesystem_snapshot = make_filesystem_snapshot(git_meta)
    payload = {
        "role": "filesystem-package",
        "built_at_utc": filesystem_snapshot["built_at_utc"],
        "pio_env": env.subst("$PIOENV"),
        "board": env.subst("$BOARD"),
        "branch": filesystem_snapshot["source_snapshot"]["branch"],
        "commit_sha": filesystem_snapshot["source_snapshot"]["commit_sha"],
        "commit_short": filesystem_snapshot["source_snapshot"]["commit_short"],
        "tag": filesystem_snapshot["source_snapshot"]["exact_tag"] or filesystem_snapshot["source_snapshot"]["nearest_tag"],
        "dirty": filesystem_snapshot["source_snapshot"]["dirty"],
        "data_tree_hash": filesystem_snapshot["data_tree_hash"],
        "code_tree_hash": code_snapshot["code_tree_hash"],
        "file_count": filesystem_snapshot["file_count"],
        "total_bytes": filesystem_snapshot["total_bytes"],
        "note": "upload and uploadfs can be done separately",
    }
    write_text_if_changed(BUILDINFO_HTML_PATH, render_buildinfo_html(payload))
    print(f"buildinfo: wrote filesystem asset -> {BUILDINFO_HTML_PATH.relative_to(PROJECT_DIR)}")


generate_firmware_metadata(env=PIO_ENV)
env.AddPreAction("buildfs", generate_filesystem_metadata)  # noqa: F821
env.AddPreAction("uploadfs", generate_filesystem_metadata)  # noqa: F821
