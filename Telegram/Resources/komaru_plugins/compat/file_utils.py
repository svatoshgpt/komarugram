# This is the source code of KomaruGram for Desktop.
#
# exteraGram file helpers (SDK 1.4.6.5), rooted in KomaruGram's plugin data
# folder, plus desktop-safe FilesController.
from dataclasses import dataclass, field
from enum import Enum
import os
import secrets
from typing import Any, Callable, Dict, List, Optional

from komaru_runtime import context


def _root(*parts):
    manager = context.manager()
    base = manager.data_dir if manager is not None else os.getcwd()
    path = os.path.join(base, *parts)
    os.makedirs(path, exist_ok=True)
    return path


def get_plugins_dir() -> str:
    return _root("installed")


def get_cache_dir() -> str:
    return _root("cache")


def get_files_dir() -> str:
    return _root("files")


def get_images_dir() -> str:
    return _root("files", "images")


def get_videos_dir() -> str:
    return _root("files", "videos")


def get_audios_dir() -> str:
    return _root("files", "audio")


def get_documents_dir() -> str:
    return _root("files", "documents")


def ensure_dir_exists(path: str) -> str:
    os.makedirs(path, exist_ok=True)
    return path


def list_dir(
    path: str,
    recursive: bool = False,
    include_files: bool = True,
    include_dirs: bool = False,
    extensions: Optional[List[str]] = None,
) -> List[str]:
    result = []
    if not os.path.isdir(path):
        return result
    norm_exts = [e.lower() for e in extensions] if extensions else None
    for root, dirs, files in os.walk(path):
        if include_dirs:
            result.extend(os.path.join(root, d) for d in dirs)
        if include_files:
            for name in files:
                if not norm_exts or any(name.lower().endswith(ext) for ext in norm_exts):
                    result.append(os.path.join(root, name))
        if not recursive:
            break
    return result


def read_file(path: str, max_size_bytes: Optional[int] = None) -> Optional[str]:
    try:
        with open(path, "r", encoding="utf-8") as f:
            return f.read(max_size_bytes) if max_size_bytes else f.read()
    except OSError:
        return None


def read_file_bytes(path: str) -> Optional[bytes]:
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError:
        return None


def write_file(path: str, content: str) -> bool:
    try:
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write(content)
        return True
    except OSError:
        return False


def write_file_bytes(path: str, content: bytes) -> bool:
    try:
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        with open(path, "wb") as f:
            f.write(content)
        return True
    except OSError:
        return False


def delete_file(path: str) -> bool:
    try:
        os.remove(path)
        return True
    except OSError:
        return False


class FilesController:
    DIRECT_FILE_ICONS = False
    SUPPORT_ICONS = False

    __config: Dict[str, "FilesController.FileInfo"] = {}
    __secrets: Dict[str, str] = {}

    class Place(Enum):
        UNKNOWN = 1
        ChatActivity = 2
        FilteredSearchView = 3
        SharedMediaLayout = 4
        SearchDownloadsContainer = 5
        ChannelAdminLogActivity = 6

    @dataclass
    class FileInfo:
        ext: str
        on_click: Callable[["FilesController.OnClickArgs"], None] = field(compare=False, repr=False)
        whitelist_places: List["FilesController.Place"] = field(default_factory=list)
        blacklist_places: List["FilesController.Place"] = field(default_factory=list)
        get_icon: Optional[Callable[[], Any]] = field(default=None, compare=False, repr=False)

        def __post_init__(self):
            if self.whitelist_places and self.blacklist_places:
                raise ValueError("Cannot use whitelist_places with blacklist_places together")

    @dataclass
    class OnClickArgs:
        place: "FilesController.Place"
        file: Any = field(compare=False, repr=False)
        file_name: str = ""
        message: Any = field(default=None, compare=False, repr=False)
        activity: Any = field(default=None, compare=False, repr=False)
        parent_fragment: Any = field(default=None, compare=False, repr=False)

    class ExtensionAlreadyRegistered(Exception):
        def __init__(self, ext: str):
            super().__init__(f"The file extension '{ext}' is already in use.")

    class ExtensionNotRegistered(Exception):
        def __init__(self, ext: str):
            super().__init__(f"The file extension '{ext}' is not registered.")

    class SecretInvalid(Exception):
        def __init__(self, ext: str, secret: str):
            super().__init__(f"Provided secret '{secret}' for extension '{ext}' is invalid.")

    @classmethod
    def register(cls, file_info: "FilesController.FileInfo") -> str:
        ext = str(file_info.ext).lstrip(".").lower()
        if ext in cls.__config:
            raise cls.ExtensionAlreadyRegistered(ext)
        record = context.current_record()
        if record is not None:
            record.unsupported("add_file_hook")
        secret = secrets.token_hex(16)
        cls.__config[ext] = file_info
        cls.__secrets[ext] = secret
        return secret

    @classmethod
    def unregister(cls, ext: str, secret: str):
        key = str(ext).lstrip(".").lower()
        if key not in cls.__config:
            raise cls.ExtensionNotRegistered(key)
        if cls.__secrets.get(key) != secret:
            raise cls.SecretInvalid(key, secret)
        cls.__config.pop(key, None)
        cls.__secrets.pop(key, None)

    @classmethod
    def _stop(cls):
        cls.__config.clear()
        cls.__secrets.clear()
