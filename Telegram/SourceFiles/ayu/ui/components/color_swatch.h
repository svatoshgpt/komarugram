// This is the source code of KomaruGram for Desktop.
//
// A small square of the colour after every #rgb, #rrggbb or #rrggbbaa in a
// message, the way code editors preview colours. The square is a custom
// emoji drawn by KomaruGram itself; it is not exported when text is copied.
#pragma once

#include "ui/text/text.h"

namespace AyuUi {

[[nodiscard]] TextWithEntities AddColorSwatches(TextWithEntities text);
[[nodiscard]] Ui::Text::MarkedContext WithColorSwatches(
	Ui::Text::MarkedContext context);

} // namespace AyuUi
