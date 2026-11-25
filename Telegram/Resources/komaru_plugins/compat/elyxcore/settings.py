# This is the source code of KomaruGram for Desktop.
#
# Settings controller matching exteraGram's elyxcore.settings (SDK 1.4.6.5).
import plugin_settings
from komaru_runtime import context


class SettingsController:
    def __init__(self, plugin_id: str):
        self.plugin_id = plugin_id

    def get_settings(self):
        return plugin_settings.get_all_settings(self.plugin_id)

    def get_setting(self, key: str, default=None):
        return plugin_settings.get_setting(self.plugin_id, key, default)

    def get(self, key: str, default=None):
        return self.get_setting(key, default)

    def set_setting(self, key: str, value, reload_settings: bool = False):
        plugin_settings.set_setting(self.plugin_id, key, value)
        if reload_settings:
            manager = context.manager()
            if manager is not None:
                record = manager.records.get(self.plugin_id)
                if record is not None:
                    manager.notify_settings(record)

    def set(self, key: str, value, reload_settings: bool = False):
        self.set_setting(key, value, reload_settings)

    def __getitem__(self, item):
        return self.get_setting(item)

    def __setitem__(self, key, value):
        self.set_setting(key, value)

    def clear_settings(self):
        plugin_settings.clear_settings(self.plugin_id)

    def __call__(self, key, default=None):
        return self.get_setting(key, default)
