// This is the source code of KomaruGram for Desktop.
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {

class GenericBox;

// Lets a person with a KomaruGram status change its emoji and caption.
void FillKomaruBadgeBox(
	not_null<GenericBox*> box,
	not_null<Window::SessionController*> controller);

} // namespace Ui
