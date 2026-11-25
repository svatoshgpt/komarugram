# This is the source code of KomaruGram for Desktop.
#
# exteraGram file helpers, rooted in KomaruGram's plugin data folder.
import os

from komaru_runtime import context


def _root(*parts):
    path = os.path.join(context.manager().data_dir, *parts)
    os.makedirs(path, exist_ok=True)
    return path


def get_plugins_dir():
    return _root("installed")


def get_cache_dir():
    return _root("cache")


def get_files_dir():
    return _root("files")


def get_images_dir():
    return _root("files", "images")


def get_videos_dir():
    return _root("files", "videos")


def get_audios_dir():
    return _root("files", "audio")


def get_documents_dir():
    return _root("files", "documents")


def ensure_dir_exists(path):
    os.makedirs(path, exist_ok=True)
    return path


def list_dir(path, recursive=False, include_files=True, include_dirs=False, extensions=None):
    result = []
    for root, dirs, files in os.walk(path):
        if include_dirs:
            result.extend(os.path.join(root, d) for d in dirs)
        if include_files:
            for name in files:
                if not extensions or os.path.splitext(name)[1].lower() in extensions:
                    result.append(os.path.join(root, name))
        if not recursive:
            break
    return result


def read_file(path, max_size_bytes=None):
    with open(path, "r", encoding="utf-8") as f:
        return f.read(max_size_bytes) if max_size_bytes else f.read()


def write_file(path, content):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    return True


def delete_file(path):
    try:
        os.remove(path)
        return True
    except OSError:
        return False
