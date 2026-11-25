# This is the source code of KomaruGram for Desktop.
#
# Desktop stand-ins for exteraGram's Dalvik/dex dynamic class generation
# helpers (extera_utils.classes). Allows Android plugins that import them to
# load cleanly on desktop.


class Base:
    def __init__(self, *args, **kwargs):
        self.java = self

    @classmethod
    def new_instance(cls, init_args=None, *args, **kwargs):
        return cls(*(init_args or ()), *args, **kwargs)

    @classmethod
    def new_java_instance(cls, *args, **kwargs):
        return cls(*args, **kwargs)

    @classmethod
    def java_class(cls):
        return cls

    def jrelease(self):
        return True


class PyObj:
    def __init__(self, value=None):
        self.value = value

    @classmethod
    def create(cls, value=None):
        return cls(value)


def java_subclass(*args, **kwargs):
    def decorator(cls):
        return cls
    return decorator


def joverride(*args, **kwargs):
    def decorator(fn):
        return fn
    return decorator


def jclassbuilder(*args, **kwargs):
    def decorator(fn):
        return fn
    return decorator


def jfield(*args, default=None, **kwargs):
    return default


def jgetmethod(*args, **kwargs):
    return None


def jsetmethod(*args, **kwargs):
    return None


def jmvelmethod(*args, **kwargs):
    return lambda *a, **kw: None
