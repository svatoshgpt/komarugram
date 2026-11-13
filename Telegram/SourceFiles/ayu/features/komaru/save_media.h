// This is the source code of KomaruGram for Desktop.
#pragma once

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Ui {
class PopupMenu;
} // namespace Ui

class DocumentData;

namespace KomaruSaveMedia {

// "Save as GIF" for videos and "Save as video" for animated stickers.
void AddActions(
	not_null<Ui::PopupMenu*> menu,
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<DocumentData*> document,
	bool allowed);

} // namespace KomaruSaveMedia
