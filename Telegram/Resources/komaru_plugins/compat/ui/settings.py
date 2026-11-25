# This is the source code of KomaruGram for Desktop.
#
# exteraGram settings rows. KomaruGram draws them natively: the host turns
# each row into a plain description and keeps the callbacks on its side.
from dataclasses import dataclass, field
from typing import Any, Callable, List, Optional


@dataclass
class Header:
    text: str = ""


@dataclass
class Divider:
    text: Optional[str] = None


@dataclass
class Switch:
    key: str = ""
    text: str = ""
    default: bool = False
    subtext: Optional[str] = None
    icon: Optional[str] = None
    on_change: Optional[Callable] = None
    on_long_click: Optional[Callable] = None
    link_alias: Optional[str] = None


@dataclass
class Selector:
    key: str = ""
    text: str = ""
    default: int = 0
    items: List[str] = field(default_factory=list)
    icon: Optional[str] = None
    on_change: Optional[Callable] = None
    on_long_click: Optional[Callable] = None
    link_alias: Optional[str] = None


@dataclass
class Input:
    key: str = ""
    text: str = ""
    default: Optional[str] = ""
    subtext: Optional[str] = None
    icon: Optional[str] = None
    on_change: Optional[Callable] = None
    on_long_click: Optional[Callable] = None
    link_alias: Optional[str] = None


@dataclass
class Text:
    text: str = ""
    subtext: Optional[str] = None
    icon: Optional[str] = None
    accent: bool = False
    red: bool = False
    on_click: Optional[Callable] = None
    on_long_click: Optional[Callable] = None
    create_sub_fragment: Optional[Callable] = None
    link_alias: Optional[str] = None


@dataclass
class EditText:
    key: str = ""
    hint: str = ""
    default: Optional[str] = ""
    multiline: bool = False
    max_length: Optional[int] = None
    mask: Optional[str] = None
    on_change: Optional[Callable] = None


@dataclass
class Custom:
    item: Any = None
    view: Any = None
    factory: Any = None
    factory_args: Any = None
    on_click: Optional[Callable] = None
    on_long_click: Optional[Callable] = None
    create_sub_fragment: Optional[Callable] = None
    link_alias: Optional[str] = None


class SimpleSettingFactory:
    def __init__(self, *args, **kwargs):
        self.instance = self
        self.java = None
