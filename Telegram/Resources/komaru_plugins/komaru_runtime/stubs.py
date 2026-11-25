# This is the source code of KomaruGram for Desktop.
#
# Android and Java modules that exteraGram plugins import. They do not
# exist on desktop, so these stand-ins accept any attribute, call or
# subclass and do nothing, which lets a plugin import cleanly and reach its
# own "not available" branches instead of failing to load at all.
import importlib.abc
import importlib.machinery
import sys
import types

PREFIXES = (
    "java", "javax", "android", "androidx", "dalvik", "kotlin",
    "org.telegram", "com.exteragram", "com.google.android", "de.robv",
    "frida_gadget_api", "dynamic_proxy", "extera_utils", "markdown_utils",
)


class _StubMeta(type):
    def __getattr__(cls, name):
        if name.startswith("__") and name.endswith("__"):
            raise AttributeError(name)
        stub = _StubMeta(name, (Stub,), {})
        setattr(cls, name, stub)
        return stub

    def __bool__(cls):
        return False

    def __iter__(cls):
        return iter(())


class Stub(metaclass=_StubMeta):
    def __init__(self, *args, **kwargs):
        pass

    def __getattr__(self, name):
        if name.startswith("__") and name.endswith("__"):
            raise AttributeError(name)
        return Stub()

    def __call__(self, *args, **kwargs):
        return Stub()

    def __bool__(self):
        return False

    def __iter__(self):
        return iter(())

    def __len__(self):
        return 0

    def __int__(self):
        return 0

    def __index__(self):
        return 0

    def __float__(self):
        return 0.0

    def __str__(self):
        return ""

    def __eq__(self, other):
        return isinstance(other, Stub)

    def __hash__(self):
        return 0


class _StubModule(types.ModuleType):
    def __getattr__(self, name):
        if name.startswith("__") and name.endswith("__"):
            raise AttributeError(name)
        stub = _StubMeta(name, (Stub,), {})
        setattr(self, name, stub)
        return stub


class _Finder(importlib.abc.MetaPathFinder, importlib.abc.Loader):
    def find_spec(self, fullname, path=None, target=None):
        if any(fullname == p
               or fullname.startswith(p + ".")
               or p.startswith(fullname + ".") for p in PREFIXES):
            return importlib.machinery.ModuleSpec(fullname, self, is_package=True)
        return None

    def create_module(self, spec):
        module = _StubModule(spec.name)
        module.__path__ = []
        return module

    def exec_module(self, module):
        pass


def install():
    sys.meta_path.append(_Finder())
