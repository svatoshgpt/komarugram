# This is the source code of KomaruGram for Desktop.
#
# exteraGram settings rows (SDK 1.4.6.5). KomaruGram draws them natively:
# the host turns each row into a plain description and keeps the callbacks
# on its side.
from dataclasses import dataclass, field
from typing import Any, Callable, List, Optional


@dataclass
class Header:
    text: str = ""
    type: str = field(default="header", init=False)


@dataclass
class Divider:
    text: Optional[str] = None
    type: str = field(default="divider", init=False)


@dataclass
class Switch:
    key: str = ""
    text: str = ""
    default: bool = False
    subtext: Optional[str] = None
    icon: Optional[str] = None
    on_change: Optional[Callable] = field(default=None, compare=False, repr=False)
    type: str = field(default="switch", init=False)
    on_long_click: Optional[Callable] = field(default=None, compare=False, repr=False)
    link_alias: Optional[str] = None


@dataclass
class Selector:
    key: str = ""
    text: str = ""
    default: int = 0
    items: List[str] = field(default_factory=list)
    icon: Optional[str] = None
    on_change: Optional[Callable] = field(default=None, compare=False, repr=False)
    type: str = field(default="selector", init=False)
    on_long_click: Optional[Callable] = field(default=None, compare=False, repr=False)
    link_alias: Optional[str] = None


@dataclass
class Input:
    key: str = ""
    text: str = ""
    default: Optional[str] = ""
    subtext: Optional[str] = None
    icon: Optional[str] = None
    on_change: Optional[Callable] = field(default=None, compare=False, repr=False)
    type: str = field(default="input", init=False)
    on_long_click: Optional[Callable] = field(default=None, compare=False, repr=False)
    link_alias: Optional[str] = None


@dataclass
class Text:
    text: str = ""
    subtext: Optional[str] = None
    icon: Optional[str] = None
    accent: bool = False
    red: bool = False
    on_click: Optional[Callable] = field(default=None, compare=False, repr=False)
    create_sub_fragment: Optional[Callable] = field(default=None, compare=False, repr=False)
    type: str = field(default="text", init=False)
    on_long_click: Optional[Callable] = field(default=None, compare=False, repr=False)
    link_alias: Optional[str] = None


@dataclass
class EditText:
    key: str = ""
    hint: str = ""
    default: Optional[str] = ""
    multiline: bool = False
    max_length: int = 256
    mask: Optional[str] = None
    on_change: Optional[Callable] = field(default=None, compare=False, repr=False)
    type: str = field(default="edit_text", init=False)


@dataclass
class Custom:
    type: str = field(default="custom", init=False)
    item: Any = field(default=None, compare=False, repr=False)
    view: Any = field(default=None, compare=False, repr=False)
    factory: Any = field(default=None, compare=False, repr=False)
    factory_args: Any = field(default=None, compare=False, repr=False)
    on_click: Optional[Callable] = field(default=None, compare=False, repr=False)
    on_long_click: Optional[Callable] = field(default=None, compare=False, repr=False)
    create_sub_fragment: Optional[Callable] = field(default=None, compare=False, repr=False)
    link_alias: Optional[str] = None


class SimpleSettingFactory:
    def __init__(
        self,
        create_view=None,
        bind_view=None,
        *,
        is_clickable: bool = False,
        is_shadow: bool = False,
        create_item=None,
        on_click=None,
        on_long_click=None,
        attached_view=None,
        equals=None,
        content_equals=None,
    ):
        self.create_view = create_view
        self.bind_view = bind_view
        self.is_clickable = is_clickable
        self.is_shadow = is_shadow
        self.create_item = create_item
        self.on_click = on_click
        self.on_long_click = on_long_click
        self.attached_view = attached_view
        self.equals = equals
        self.content_equals = content_equals
        self.instance = self
        self.java = None

    def release(self):
        return True

    def __call__(self, *args, create_sub_fragment=None, link_alias=None):
        return Custom(
            factory=self.java or self,
            factory_args=args if args else None,
            link_alias=link_alias,
            create_sub_fragment=create_sub_fragment,
        )
