# This is the source code of KomaruGram for Desktop.
#
# exteraGram dialogs. A desktop box would need a round trip for every
# button, so the dialog is shown as a toast with its title and message and
# the buttons are not offered.
from komaru_runtime import protocol


class AlertDialogBuilder:
    ALERT_TYPE_MESSAGE = 0
    ALERT_TYPE_SPINNER = 1
    ALERT_TYPE_LOADING = 2
    BUTTON_POSITIVE = -1
    BUTTON_NEGATIVE = -2
    BUTTON_NEUTRAL = -3

    def __init__(self, *args, **kwargs):
        self._title = ""
        self._message = ""

    def set_title(self, title):
        self._title = str(title)
        return self

    def set_message(self, message):
        self._message = str(message)
        return self

    def show(self):
        text = self._title
        if self._message:
            text = (text + "\n" if text else "") + self._message
        if text:
            protocol.event("toast", text=text)
        return self

    def dismiss(self):
        pass

    def __getattr__(self, name):
        def chain(*args, **kwargs):
            return self
        return chain
