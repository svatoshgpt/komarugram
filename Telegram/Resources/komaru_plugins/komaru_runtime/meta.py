# This is the source code of KomaruGram for Desktop.
#
# Reads plugin metadata without running plugin code: top-level constants of
# a single-file plugin through ast, and the refmap and metainfo files of an
# Elyx archive.
import ast
import json
import re
import zipfile

ID_PATTERN = re.compile(r"^[A-Za-z][A-Za-z0-9_-]{1,31}$")
KEYS = (
    "id", "name", "description", "author", "version", "icon",
    "app_version", "sdk_version", "min_version", "requirements",
    "requires", "runtime", "binaries",
)
ELYX_EXTENSIONS = (".elyx", ".eaf", ".elyx.zip", ".eaf.zip", ".zip")


class MetaError(Exception):
    pass


def normalize(raw):
    result = {}
    for key, value in raw.items():
        key = str(key)
        if key.startswith("__") and key.endswith("__"):
            key = key[2:-2]
        result[key] = value
    if "min_version" in result and "app_version" not in result:
        result["app_version"] = ">=" + str(result["min_version"])
    result.setdefault("description", "")
    result.setdefault("author", "")
    result.setdefault("version", "1.0.0")
    result.setdefault("icon", "")
    result.setdefault("runtime", "python")
    for key in ("id", "name", "description", "author", "version", "icon"):
        if result.get(key) is not None:
            result[key] = str(result[key])
    plugin_id = result.get("id", "")
    if not ID_PATTERN.match(plugin_id):
        raise MetaError("Plugin id %r is not valid" % plugin_id)
    if not result.get("name"):
        result["name"] = plugin_id
    return result


def from_source(source):
    try:
        tree = ast.parse(source)
    except SyntaxError as error:
        raise MetaError("Plugin source does not parse: %s" % error)
    raw = {}
    for node in tree.body:
        if not isinstance(node, ast.Assign) or len(node.targets) != 1:
            continue
        target = node.targets[0]
        if not isinstance(target, ast.Name):
            continue
        name = target.id
        if not (name.startswith("__") and name.endswith("__")):
            continue
        if name[2:-2] not in KEYS:
            continue
        try:
            raw[name] = ast.literal_eval(node.value)
        except ValueError:
            continue
    if "__id__" not in raw:
        raise MetaError("Plugin has no __id__")
    return normalize(raw)


def _scalar(text):
    text = text.strip()
    if not text:
        return ""
    if text[0] in "'\"" and text[-1] == text[0] and len(text) > 1:
        inner = text[1:-1]
        return inner.replace("''", "'") if text[0] == "'" else inner
    lowered = text.lower()
    if lowered in ("true", "yes"):
        return True
    if lowered in ("false", "no"):
        return False
    if lowered in ("null", "~"):
        return None
    return text


def parse_yaml(text):
    try:
        import yaml
        value = yaml.safe_load(text)
        return value if isinstance(value, dict) else {}
    except ImportError:
        pass
    # Enough of YAML for metainfo and refmap files: top-level keys with
    # scalar values, and one level of nested mappings.
    result = {}
    current = None
    for raw in text.splitlines():
        line = raw.split(" #", 1)[0].rstrip()
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        indented = line[0] in " \t"
        key, sep, value = line.strip().partition(":")
        if not sep:
            continue
        if indented and current is not None:
            current[key.strip()] = _scalar(value)
        elif value.strip():
            result[key.strip()] = _scalar(value)
            current = None
        else:
            current = {}
            result[key.strip()] = current
    return result


def parse_mapping(name, data):
    text = data.decode("utf-8-sig")
    if name.endswith(".json"):
        value = json.loads(text)
        return value if isinstance(value, dict) else {}
    if name.endswith(".py"):
        return from_source(text)
    return parse_yaml(text)


def is_archive(path):
    lowered = path.lower()
    return lowered.endswith(ELYX_EXTENSIONS) and zipfile.is_zipfile(path)


def read_refmap(archive):
    names = set(archive.namelist())
    for candidate in ("refmap.yaml", "refmap.yml", "refmap.json"):
        if candidate in names:
            return parse_mapping(candidate, archive.read(candidate))
    return {}


def from_archive(path):
    with zipfile.ZipFile(path) as archive:
        names = set(archive.namelist())
        refmap = read_refmap(archive)
        metainfo = refmap.get("metainfo")
        if not metainfo:
            for candidate in ("metainfo.yaml", "metainfo.yml", "metainfo.json"):
                if candidate in names:
                    metainfo = candidate
                    break
        if not metainfo or metainfo not in names:
            raise MetaError("Archive has no metainfo file")
        meta = normalize(parse_mapping(metainfo, archive.read(metainfo)))
        main = refmap.get("main") or ("main.py" if "main.py" in names else "")
        if meta["runtime"] == "python" and main not in names:
            raise MetaError("Archive has no entry module")
        meta["refmap"] = refmap
        meta["main"] = main
        return meta
