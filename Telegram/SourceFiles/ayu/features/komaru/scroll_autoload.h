// This is the source code of KomaruGram for Desktop.
#pragma once

namespace KomaruAutoload {

// Media that flies past while a chat scrolls fast does not start loading from the cloud.
[[nodiscard]] bool Paused();

// Called on every scroll of a chat; resumed() repaints it once scrolling slows down.
void Scrolled(int delta, int viewHeight, Fn<void()> resumed);

} // namespace KomaruAutoload
