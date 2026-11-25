# This is the source code of KomaruGram for Desktop.
#
# Localization helper matching exteraGram's elyxcore.localization (SDK 1.4.6.5).
from __future__ import annotations

import locale
from typing import Any, Dict, List, Optional

import plugin_settings
from elyxcore.utils import LazyDict

ELYX_ID = "elyxcore"


class Strings(LazyDict):
    def __init__(self, all_strings: Dict[str, Any]):
        super().__init__()
        self.all_strings = all_strings or {}

    @staticmethod
    def get_current_language() -> str:
        forced = plugin_settings.get_setting(ELYX_ID, "language", "")
        if forced:
            return str(forced)
        try:
            lang = locale.getlocale()[0] or ""
            if lang:
                return lang.split("_")[0].lower()
        except Exception:
            pass
        return "en"

    def _lang_dict(self, lang: str) -> Dict[str, Any]:
        value = self.all_strings.get(lang)
        if isinstance(value, dict):
            return value
        en = self.all_strings.get("en")
        if isinstance(en, dict):
            return en
        return self.all_strings

    def __getitem__(self, key):
        if key == "all_strings":
            return super().__getitem__(key)

        res = self._lang_dict(self.get_current_language()).get(key, None)
        if res is None:
            en = self.all_strings.get("en")
            if isinstance(en, dict):
                res = en.get(key, None)
        if res is None:
            return key
        return res

    def __setitem__(self, key, value):
        if key != "all_strings":
            raise RuntimeError("Strings are read-only")
        super().__setitem__(key, value)

    def get(self, key: str, default=None) -> Any:
        return self.get_with_locale(key, default)

    def get_with_locale(self, key: str, default=None, locale: Optional[str] = None) -> Any:
        lang = locale or self.get_current_language()
        res = self._lang_dict(lang).get(key, None)
        if res is None:
            if default is not None:
                return default
            en = self.all_strings.get("en")
            if isinstance(en, dict):
                res = en.get(key, None)
        if res is None:
            return key
        return res

    def __call__(self, key: str, *args, default: Optional[str] = None, locale: Optional[str] = None, **kwargs):
        result = self.get_with_locale(key, default, locale)
        if isinstance(result, str) and (args or kwargs):
            result = result.format(*args, **kwargs)
        return result

    @staticmethod
    def pluralization_string(number: int, words: List[str]) -> str:
        if number % 10 == 1 and number % 100 != 11:
            return f"{number} {words[0]}"
        elif 2 <= number % 10 <= 4 and (number % 100 < 10 or number % 100 >= 20):
            return f"{number} {words[1]}"
        else:
            return f"{number} {words[2]}"

    def pluralize(self, number: int, key: str) -> str:
        return self.pluralization_string(number, self(key))
