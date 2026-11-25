# This is the source code of KomaruGram for Desktop.
#
# exteraGram reflection helpers. There are no Java classes on desktop, so
# lookups find nothing and plugins take their "class not found" path.


def find_class(name, *args, **kwargs):
    return None


def get_private_field(obj, name, *args, **kwargs):
    return getattr(obj, name, None)


def set_private_field(obj, name, value, *args, **kwargs):
    try:
        setattr(obj, name, value)
        return True
    except Exception:
        return False


def get_static_private_field(cls, name, *args, **kwargs):
    return getattr(cls, name, None)


def set_static_private_field(cls, name, value, *args, **kwargs):
    return set_private_field(cls, name, value)
