# This is the source code of KomaruGram for Desktop.
#
# Asset helpers matching exteraGram's elyxcore.assets (SDK 1.4.6.5), with
# backwards compatibility for KomaruGram's path/exists/read_bytes/read_text.
from __future__ import annotations

import json
import os
import string
from pathlib import Path
from typing import Optional, Union
from zipfile import ZipFile, ZipInfo

from file_utils import get_cache_dir
from komaru_runtime import meta as metamod


def load_data_file(path: Union[Path, ZipInfo, str, bytes], plzip: Optional[ZipFile] = None) -> dict:
    if isinstance(path, bytes):
        content = path.decode("utf-8-sig")
        suffix = ""
    elif isinstance(path, Path):
        content = path.read_text(encoding="utf-8-sig")
        suffix = path.suffix[1:].lower()
    else:
        filename = path.filename if isinstance(path, ZipInfo) else str(path)
        content = plzip.read(filename).decode("utf-8-sig")
        suffix = filename.split("/")[-1].split(".")[-1].lower()

    if suffix == "json":
        return json.loads(content)
    if suffix in ("yaml", "yml"):
        return metamod.parse_yaml(content)
    return {}


def normalize_name(name: str) -> str:
    allowed = string.ascii_letters + string.digits
    return "".join(ch if ch in allowed else "_" for ch in name)


class AssetNotFoundException(Exception):
    def __init__(self, name: str, dir_path: Optional[str] = None):
        super().__init__(f'Asset "{name}" not found in directory {dir_path}')


class AssetsDirNotFoundException(Exception):
    def __init__(self, dir_path: Optional[str] = None):
        super().__init__(f"Directory {dir_path} does not exist")


class Asset:
    def __init__(self, dir_path: Union[str, Path], filename: str, name: str):
        if not dir_path or not filename or not name:
            raise ValueError("dir_path, filename and name must be provided")

        if hasattr(dir_path, "getAbsolutePath"):
            dir_path = str(dir_path.getAbsolutePath())

        self.dir_path = dir_path if isinstance(dir_path, Path) else Path(dir_path)
        self.name = normalize_name(name)
        self.filename = filename
        self.ext = filename.split(".")[-1] if "." in filename else ""

        if not self.dir_path.exists():
            raise AssetsDirNotFoundException(str(self.dir_path))
        if not self.dir_path.is_dir():
            raise ValueError(f"Path {dir_path} is not a directory")
        if not self.path.exists():
            raise AssetNotFoundException(name, str(dir_path))

    @classmethod
    def from_path(cls, path: Union[str, Path]) -> "Asset":
        if hasattr(path, "getAbsolutePath"):
            path = path.getAbsolutePath()
        if isinstance(path, str):
            path = Path(path)
        if not path.exists():
            raise FileNotFoundError(f"Path {path} does not exist")
        if not path.is_file():
            raise IsADirectoryError(f"Path {path} is not a file")
        return cls(path.parent, path.name, path.name.split(".")[0])

    @classmethod
    def temp_asset_from_url(cls, url: str, filename: str) -> "Asset":
        import urllib.request

        dir_path = Path(get_cache_dir()) / "elyx_assets"
        dir_path.mkdir(parents=True, exist_ok=True)
        temp_file = dir_path / filename
        with urllib.request.urlopen(url) as resp, open(temp_file, "wb") as f:
            while True:
                chunk = resp.read(8192)
                if not chunk:
                    break
                f.write(chunk)
        stem = "_".join(filename.split(".")[:-1]) or filename
        return cls(dir_path, filename, stem)

    @property
    def path_str(self) -> str:
        return str(self.path)

    @property
    def path(self) -> Path:
        return self.dir_path.joinpath(self.filename)

    @property
    def java_file(self):
        return self.path_str

    def to_image_location(self):
        return None

    def to_drawable(self):
        return None

    def to_bitmap_drawable(self, width: int = 32, height: int = 32):
        return None

    def to_lottie_drawable(self, width: int = 32, height: int = 32):
        return None

    def to_svg_drawable(self, width: Optional[int] = None, height: Optional[int] = None, *args):
        return None

    def to_svg_thumb(self, color_key: int, alpha: float):
        return None

    def to_svg_bitmap(self, width: int = 32, height: int = 32, white: bool = False):
        return None

    def content_bytes(self) -> bytes:
        return self.path.read_bytes()

    def content_string(self, encoding: str = "utf-8") -> str:
        return self.path.read_text(encoding=encoding)

    def content_json(self) -> dict:
        return load_data_file(self.path)

    def content_yaml(self) -> dict:
        return load_data_file(self.path)

    def content(self) -> Union[dict, str, bytes]:
        if self.ext.lower() in ("json", "yaml", "yml"):
            return load_data_file(self.path)
        try:
            return self.content_string()
        except Exception:
            return self.content_bytes()

    def __str__(self):
        return self.__repr__()

    def __repr__(self):
        return f"<Elyx.Asset name={self.name} path={self.path_str} ext={self.ext}>"


class Assets:
    def __init__(self, dir_path: Union[str, Path]):
        self.root = str(dir_path or "")
        self.dir_path = Path(dir_path) if dir_path else None
        self.files = {}
        self.names = {}
        if self.dir_path is not None and self.dir_path.is_dir():
            self.files = {f.name: f for f in self.dir_path.iterdir()}
            self.names = {
                normalize_name(f.name.split(".")[0]): f.name
                for f in self.files.values()
            }

    @property
    def parent(self) -> "Assets":
        if self.dir_path is None:
            raise AssetsDirNotFoundException("")
        return Assets(self.dir_path.parent)

    def path(self, name: str) -> str:
        full = os.path.normpath(os.path.join(self.root, name))
        root_norm = os.path.normpath(self.root)
        if full != root_norm and not full.startswith(root_norm + os.sep):
            raise ValueError("Asset path leaves the assets folder")
        return full

    def exists(self, name: str) -> bool:
        return bool(self.root) and os.path.exists(self.path(name))

    def read_bytes(self, name: str) -> bytes:
        with open(self.path(name), "rb") as f:
            return f.read()

    def read_text(self, name: str, encoding: str = "utf-8") -> str:
        return self.read_bytes(name).decode(encoding)

    def get(self, name: str) -> Union[Asset, "Assets"]:
        if self.dir_path is None or not self.dir_path.is_dir():
            raise AssetNotFoundException(name, self.root)

        if "/" in name:
            parts = name.split("/", 1)
            return self.get(parts[0]).get(parts[1])

        if name not in self.names and name not in self.files:
            raise AssetNotFoundException(name, str(self.dir_path))

        filename = self.names[name] if name in self.names else name
        target = self.dir_path.joinpath(filename)
        if target.is_dir():
            return Assets(target)

        return Asset(
            self.dir_path,
            filename,
            name if name in self.names else name.split(".")[0],
        )

    def __getitem__(self, item):
        return self.get(item)

    def __getattr__(self, item):
        if item.startswith("__") and item.endswith("__"):
            raise AttributeError(item)
        return self.get(item)

    def __contains__(self, item):
        return item in self.names or item in self.files

    def __iter__(self):
        return iter(self.files)

    def __len__(self):
        return len(self.files)

    def __repr__(self):
        return f"<Elyx.Assets dir_path={self.dir_path}>"

    def __str__(self):
        return self.__repr__()
