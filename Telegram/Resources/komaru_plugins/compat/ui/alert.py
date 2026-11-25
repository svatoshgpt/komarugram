# This is the source code of KomaruGram for Desktop.
#
# exteraGram dialogs (SDK 1.4.6.5). A desktop box would need a round trip for
# every button, so the dialog is shown as a toast with its title and message
# and the buttons are not offered.
from komaru_runtime import protocol


class AlertDialogBuilder:
    ALERT_TYPE_MESSAGE = 0
    ALERT_TYPE_LOADING = 2
    ALERT_TYPE_SPINNER = 3
    BUTTON_POSITIVE = -1
    BUTTON_NEGATIVE = -2
    BUTTON_NEUTRAL = -3

    def __init__(self, context=None, progress_style=ALERT_TYPE_MESSAGE, resources_provider=None, *args, **kwargs):
        self._context = context
        self._progress_style = progress_style
        self._title = ""
        self._message = ""
        self._red_buttons = []

    def get_context(self):
        return self._context

    def set_title(self, title):
        self._title = str(title)
        return self

    def set_message(self, message):
        self._message = str(message)
        return self

    def make_button_red(self, button_type):
        if button_type not in self._red_buttons:
            self._red_buttons.append(button_type)
        return self

    def create(self):
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

    def get_dialog(self):
        return None

    def get_button(self, button_type):
        return None

    def set_progress(self, progress):
        pass

    def set_cancelable(self, cancelable):
        pass

    def set_canceled_on_touch_outside(self, cancel):
        pass

    def __getattr__(self, name):
        def chain(*args, **kwargs):
            return self
        return chain
