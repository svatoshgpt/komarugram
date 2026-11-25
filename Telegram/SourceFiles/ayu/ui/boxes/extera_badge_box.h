// This file is part of KomaruGram for Desktop, see https://github.com/svatoshgpt/komarugram
#pragma once

class PeerData;

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {

class GenericBox;

// What an exteraGram supporter arrow gives, with its number from exteraGram.
void FillExteraBadgeBox(
	not_null<GenericBox*> box,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

} // namespace Ui
