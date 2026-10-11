#!/usr/bin/env python3
"""Install the pinned upstream projectM collection on Linux."""
import argparse
import csv
import hashlib
from pathlib import Path
import shutil
import tempfile
import urllib.request
import zipfile


def archive(path, folder, repo, revision, expected):
    if path is None:
        path = folder / (repo + ".zip")
        request = urllib.request.Request(
            f"https://api.github.com/repos/projectM-visualizer/{repo}/zipball/{revision}",
            headers={"User-Agent": "Pi-Visualizer"},
        )
        print(f"Downloading {repo}...", flush=True)
        with urllib.request.urlopen(request, timeout=120) as response, path.open("wb") as output:
            shutil.copyfileobj(response, output)
    with path.open("rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()
    if digest != expected:
        raise RuntimeError(f"Archive verification failed: {repo}")
    return path


def install(args):
    destination = args.app_directory / "presets" / "Cream-of-the-Crop"
    if destination.exists():
        raise RuntimeError(f"Collection already installed: {destination}")
    if not (args.app_directory / "Pi-Visualizer").is_file():
        raise RuntimeError("App directory must contain the Pi-Visualizer executable")
    with tempfile.TemporaryDirectory(prefix=".presets-", dir=args.app_directory) as temporary:
        folder = Path(temporary)
        presets = archive(
            args.preset_archive, folder, "presets-cream-of-the-crop",
            "0180df21f5e0bd39b9060cc5de420ed2f1f9e509",
            "454c2259c39604db6d6c3a5c2ddb88f02c4e1eaa967689a84d90518c564f6f8a",
        )
        textures = archive(
            args.texture_archive, folder, "presets-milkdrop-texture-pack",
            "6368812f27bc747b517218fbf89d21d59afce4d9",
            "d3c31a91f74ed88294775af9ab652745317e867088b37b94b6820706685beb98",
        )
        pack = folder / "Cream-of-the-Crop"
        pack.mkdir()
        rows = []
        with zipfile.ZipFile(presets) as source:
            for entry in source.infolist():
                name = Path(entry.filename).name
                if name.endswith(".milk"):
                    suffix = hashlib.sha256(entry.filename.encode()).hexdigest()[:12].upper()
                    filename = f"{Path(name).stem[:100]}-{suffix}.milk"
                    target = pack / filename
                    if target.exists():
                        raise RuntimeError(f"Duplicate preset filename: {filename}")
                    with source.open(entry) as stream, target.open("wb") as output:
                        shutil.copyfileobj(stream, output)
                    rows.append((filename, entry.filename))
                elif name in ("LICENSE.md", "README.md"):
                    (pack / ("UPSTREAM-" + name)).write_bytes(source.read(entry))
        if len(rows) != 9795:
            raise RuntimeError(f"Expected 9795 presets, found {len(rows)}")
        with (pack / "preset-index.csv").open("w", newline="", encoding="utf-8") as output:
            writer = csv.writer(output)
            writer.writerow(("Installed", "Original"))
            writer.writerows(rows)
        count = 0
        with zipfile.ZipFile(textures) as source:
            for entry in source.infolist():
                name = Path(entry.filename).name
                if "/textures/" in entry.filename and not entry.is_dir():
                    target = pack / name
                    if target.exists():
                        raise RuntimeError(f"Duplicate texture filename: {name}")
                    target.write_bytes(source.read(entry))
                    count += 1
                elif name == "README.md":
                    (pack / "UPSTREAM-textures-README.md").write_bytes(source.read(entry))
        if count != 67:
            raise RuntimeError(f"Expected 67 textures, found {count}")
        pack.rename(destination)
        print(f"Installed {len(rows)} presets and {count} textures. Restart Pi-Visualizer.")
        print("Authors retain their rights; see UPSTREAM-LICENSE.md in the collection folder.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app-directory", type=Path, default=Path(__file__).resolve().parents[1] / "build")
    parser.add_argument("--preset-archive", type=Path)
    parser.add_argument("--texture-archive", type=Path)
    install(parser.parse_args())
