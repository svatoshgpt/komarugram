# This is the source code of KomaruGram for Desktop.
#
# exteraGram bulletins become KomaruGram toasts.
from komaru_runtime import protocol


def _toast(text, *args, **kwargs):
    protocol.event("toast", text=str(text))


class BulletinHelper:
    show_info = staticmethod(_toast)
    show_error = staticmethod(_toast)
    show_success = staticmethod(_toast)
    show_simple = staticmethod(_toast)
    show_two_line = staticmethod(lambda title, subtitle="", *a, **k: _toast(
        title + ("\n" + subtitle if subtitle else "")))
    show_copied_to_clipboard = staticmethod(lambda *a, **k: _toast("Copied"))
    show_link_copied = staticmethod(lambda *a, **k: _toast("Link copied"))
    show_file_saved_to_downloads = staticmethod(lambda *a, **k: _toast("Saved"))

    @staticmethod
    def show_with_button(text, *args, **kwargs):
        _toast(text)
