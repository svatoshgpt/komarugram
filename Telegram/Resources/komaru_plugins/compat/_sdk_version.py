# This is the source code of KomaruGram for Desktop.
#
# Compatibility shim for exteraGram's _sdk_version (SDK 1.4.6.5).
__version__ = "1.4.6.5"
version = (1, 4, 6, 5)
version_str = __version__

__beta__ = False
beta = __beta__


class SafeModeImporter:
    IS_OLD_VERSION = False
    I = True
    E = False
    _hooks = []

    @classmethod
    def c(cls):
        pass


Z = SafeModeImporter.INSTANCE = SafeModeImporter()


class X:
    BUILD_VERSION = 66999


def check_safemode():
    return True


def __start__():
    return True


def __stop__():
    from extera_utils.lifecycle import stop_all
    stop_all()
