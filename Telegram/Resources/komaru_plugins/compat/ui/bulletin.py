# This is the source code of KomaruGram for Desktop.
#
# exteraGram bulletins (SDK 1.4.6.5) become KomaruGram toasts.
from komaru_runtime import protocol


def _toast(text, *args, **kwargs):
    protocol.event("toast", text=str(text))


class BulletinHelper:
    DURATION_SHORT = 1500
    DURATION_LONG = 2750
    DURATION_PROLONG = 5000

    show_info = staticmethod(_toast)
    show_error = staticmethod(_toast)
    show_success = staticmethod(_toast)
    show_simple = staticmethod(_toast)

    @staticmethod
    def show_two_line(title, subtitle="", *args, **kwargs):
        _toast(str(title) + ("\n" + str(subtitle) if subtitle else ""))

    @staticmethod
    def show_with_button(text, *args, **kwargs):
        _toast(text)

    @staticmethod
    def show_undo(text, on_undo=None, on_action=None, subtitle=None, fragment=None):
        _toast(str(text) + ("\n" + str(subtitle) if subtitle else ""))
        if callable(on_action):
            on_action()

    @staticmethod
    def show_copied_to_clipboard(message=None, fragment=None):
        _toast(message if message is not None else "Copied")

    @staticmethod
    def show_link_copied(is_private_link_info=False, fragment=None):
        _toast("Link copied")

    @staticmethod
    def show_file_saved_to_gallery(is_video=False, amount=1, fragment=None):
        kind = "Video" if is_video else "Photo"
        _toast(f"{kind} saved")

    @staticmethod
    def show_file_saved_to_downloads(file_type_enum_name="UNKNOWN", amount=1, fragment=None):
        _toast("Saved")
