# This is the source code of KomaruGram for Desktop.
#
# Reads a plugin's code before it first runs and lists what can hurt the
# computer or the account, for the install box. It only reads the syntax
# tree, so nothing of the plugin is executed and obfuscated code can hide;
# then the box says the code is unreadable.
import ast
import os

# Modules by what they give a plugin.
_MODULES = {
    "subprocess": "shell", "pty": "shell", "pexpect": "shell",
    "ctypes": "native", "cffi": "native", "_ctypes": "native",
    "winreg": "registry", "_winreg": "registry",
    "pynput": "keyboard", "keyboard": "keyboard", "mouse": "keyboard",
    "pyHook": "keyboard", "win32api": "keyboard", "win32con": "keyboard",
    "marshal": "hidden_code",
    "socket": "network", "urllib": "network", "requests": "network",
    "aiohttp": "network", "httpx": "network", "http": "network",
    "smtplib": "network", "ftplib": "network",
}
_CALLS = {
    "eval": "hidden_code", "exec": "hidden_code", "compile": "hidden_code",
    "__import__": "hidden_code",
    "os.system": "shell", "os.popen": "shell", "os.startfile": "shell",
    "os.execv": "shell", "os.execl": "shell", "os.spawnv": "shell",
    "os.spawnl": "shell", "os.remove": "files", "os.unlink": "files",
    "os.rmdir": "files", "shutil.rmtree": "files",
    "os.path.expanduser": "files", "os.getenv": "files",
}
# Strings that point at the client's data or other programs' secrets.
_WORDS = {
    "tdata": "secrets", "key_datas": "secrets", "auth_key": "secrets",
    "authkey": "secrets", "appdata": "secrets", "login data": "secrets",
    "cookies": "secrets", "wallet.dat": "secrets",
}
_ORDER = ["secrets", "shell", "native", "hidden_code", "keyboard",
          "registry", "files", "network", "unreadable"]


def _dotted(node):
    parts = []
    while isinstance(node, ast.Attribute):
        parts.append(node.attr)
        node = node.value
    if isinstance(node, ast.Name):
        parts.append(node.id)
        return ".".join(reversed(parts))
    return ""


def _scan_source(source, found):
    try:
        tree = ast.parse(source)
    except (SyntaxError, ValueError):
        found.add("unreadable")
        return
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for alias in node.names:
                kind = _MODULES.get(alias.name.split(".")[0])
                if kind:
                    found.add(kind)
        elif isinstance(node, ast.ImportFrom) and node.module and not node.level:
            kind = _MODULES.get(node.module.split(".")[0])
            if kind:
                found.add(kind)
        elif isinstance(node, ast.Call):
            kind = _CALLS.get(_dotted(node.func))
            if kind:
                found.add(kind)
        elif isinstance(node, ast.Constant) and isinstance(node.value, str):
            lowered = node.value.lower()
            for word, kind in _WORDS.items():
                if word in lowered:
                    found.add(kind)
    # Long encoded blobs are how code gets hidden from this very check.
    if any(len(line) > 2000 for line in source.splitlines()):
        found.add("unreadable")


def scan(path):
    """The kinds of risk found in a file or a plugin folder, worst first."""
    found = set()
    files = []
    if os.path.isdir(path):
        for root, _, names in os.walk(path):
            for name in names:
                if name.endswith(".py"):
                    files.append(os.path.join(root, name))
                elif name.endswith((".so", ".pyd", ".dll", ".exe")):
                    found.add("native")
    else:
        files.append(path)
    for name in files:
        try:
            with open(name, "r", encoding="utf-8-sig") as f:
                _scan_source(f.read(), found)
        except (OSError, UnicodeDecodeError):
            found.add("unreadable")
    return [kind for kind in _ORDER if kind in found]
