"""
Pre-build script: gzip-compress web assets in data/ before uploadfs.

The ESP32 WebServer will transparently serve file.js.gz when a browser
requests file.js, so no HTML/JS changes are needed.

Files already smaller than their compressed version are left as-is.
"""

import gzip
import os
import shutil

Import("env")  # noqa: F821  (PlatformIO SCons environment)

DATA_DIR = os.path.join(env["PROJECT_DIR"], "data")  # noqa: F821
EXTENSIONS = (".html", ".js", ".css", ".json")
SCRIPTS_DIR = os.path.join(DATA_DIR, "scripts")
SCRIPTS_MANIFEST = os.path.join(SCRIPTS_DIR, "index.txt")
LEGACY_SCRIPTS_MANIFEST = os.path.join(SCRIPTS_DIR, ".index")
TMP_BACKUP_DIR = os.path.join(env["PROJECT_DIR"], ".pio", "tmp", "compress_data_backup")  # noqa: F821
OPTIONAL_PRUNE_DIRS = [
]
EXCLUDED_DIRS = [
    os.path.join(DATA_DIR, "www", "help"),
]


def clear_backup_dir():
    if os.path.isdir(TMP_BACKUP_DIR):
        shutil.rmtree(TMP_BACKUP_DIR)


def backup_and_remove_source(src_path):
    rel_path = os.path.relpath(src_path, DATA_DIR)
    backup_path = os.path.join(TMP_BACKUP_DIR, rel_path)
    os.makedirs(os.path.dirname(backup_path), exist_ok=True)
    shutil.copy2(src_path, backup_path)
    os.remove(src_path)


def backup_and_remove_tree(src_dir):
    if not os.path.isdir(src_dir):
        return 0

    rel_path = os.path.relpath(src_dir, DATA_DIR)
    backup_path = os.path.join(TMP_BACKUP_DIR, rel_path)
    if os.path.isdir(backup_path):
        shutil.rmtree(backup_path)
    os.makedirs(os.path.dirname(backup_path), exist_ok=True)
    shutil.copytree(src_dir, backup_path)

    removed_files = 0
    for _root, _dirs, files in os.walk(src_dir):
        removed_files += len(files)
    shutil.rmtree(src_dir)
    return removed_files


def restore_backup(source=None, target=None, env=None):  # noqa: F821
    if not os.path.isdir(TMP_BACKUP_DIR):
        return

    restored = 0
    for root, _dirs, files in os.walk(TMP_BACKUP_DIR):
        for name in files:
            backup_path = os.path.join(root, name)
            rel_path = os.path.relpath(backup_path, TMP_BACKUP_DIR)
            dst_path = os.path.join(DATA_DIR, rel_path)
            os.makedirs(os.path.dirname(dst_path), exist_ok=True)
            shutil.copy2(backup_path, dst_path)
            restored += 1

    clear_backup_dir()
    print(f"compress_data: restored {restored} source file(s) from temporary backup.")


def write_scripts_manifest():
    """Write one script filename per line so firmware can list scripts reliably."""
    if not os.path.isdir(SCRIPTS_DIR):
        return

    names = []
    for name in os.listdir(SCRIPTS_DIR):
        path = os.path.join(SCRIPTS_DIR, name)
        if not os.path.isfile(path):
            continue
        if name.startswith("."):
            continue
        if name.endswith(".gz"):
            continue
        names.append(name)

    names.sort(key=str.lower)
    content = "\n".join(names) + ("\n" if names else "")
    with open(SCRIPTS_MANIFEST, "w", encoding="utf-8") as f:
        f.write(content)

    # Cleanup legacy hidden manifest from earlier iterations.
    if os.path.exists(LEGACY_SCRIPTS_MANIFEST):
        os.remove(LEGACY_SCRIPTS_MANIFEST)


def compress_assets(source, target, env):  # noqa: F821
    if not os.path.isdir(DATA_DIR):
        print(f"compress_data: data dir not found ({DATA_DIR}), skipping.")
        return

    # Keep a deterministic script index in the filesystem image.
    write_scripts_manifest()

    # Start fresh backup space for this packaging run.
    clear_backup_dir()

    pruned_files = 0
    for prune_dir in OPTIONAL_PRUNE_DIRS:
        removed_in_dir = backup_and_remove_tree(prune_dir)
        if removed_in_dir > 0:
            rel_pruned = os.path.relpath(prune_dir, DATA_DIR)
            print(f"compress_data: pruned optional directory for FS image: {rel_pruned} ({removed_in_dir} files)")
            pruned_files += removed_in_dir

    compressed = 0
    skipped = 0
    removed = 0
    replaced = 0

    for root, _dirs, files in os.walk(DATA_DIR):
        # Never modify excluded trees (e.g. documentation sources in /www/help).
        _dirs[:] = [
            d for d in _dirs
            if os.path.join(root, d) not in EXCLUDED_DIRS
        ]

        rel_root = os.path.relpath(root, DATA_DIR)
        in_scripts_dir = rel_root == "scripts" or rel_root.startswith("scripts" + os.sep)

        # Keep script sources uncompressed for /scripts API and reclaim space
        # by removing stale script .gz artifacts from previous runs.
        if in_scripts_dir:
            for name in files:
                if not name.endswith(".gz"):
                    continue
                gz_path = os.path.join(root, name)
                if os.path.exists(gz_path):
                    os.remove(gz_path)
                    removed += 1

        for name in files:
            # Skip files that are already gzip archives
            if name.endswith(".gz"):
                continue
            if in_scripts_dir:
                skipped += 1
                continue
            if not name.endswith(EXTENSIONS):
                continue

            src_path = os.path.join(root, name)
            gz_path  = src_path + ".gz"

            raw = open(src_path, "rb").read()
            gz_data = gzip.compress(raw, compresslevel=9)

            if len(gz_data) >= len(raw):
                skipped += 1
                # Remove stale .gz if the original is now smaller
                if os.path.exists(gz_path):
                    os.remove(gz_path)
                continue

            with open(gz_path, "wb") as f:
                f.write(gz_data)

            # For non-script assets we keep only the .gz payload to minimize
            # filesystem footprint. ESPAsyncWebServer can serve .gz transparently.
            if os.path.exists(src_path):
                backup_and_remove_source(src_path)
                replaced += 1

            ratio = 100 * len(gz_data) // len(raw)
            rel   = os.path.relpath(src_path, DATA_DIR)
            print(f"  [gz] {rel:40s}  {len(raw):6d} → {len(gz_data):5d} bytes ({ratio}%)")
            compressed += 1

    print(
        f"compress_data: {compressed} file(s) compressed, {skipped} skipped "
        f"(already optimal or kept plain), {removed} stale .gz removed, "
        f"{replaced} source file(s) replaced by .gz, {pruned_files} optional file(s) pruned."
    )


# Hook runs before the filesystem image is built
env.AddPreAction("buildfs", compress_assets)  # noqa: F821
env.AddPreAction("uploadfs", compress_assets)  # noqa: F821
env.AddPostAction("buildfs", restore_backup)  # noqa: F821
env.AddPostAction("uploadfs", restore_backup)  # noqa: F821
