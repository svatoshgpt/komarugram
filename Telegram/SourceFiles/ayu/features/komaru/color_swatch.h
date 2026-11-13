// This is the source code of KomaruGram for Desktop.
#pragma once

#include "ui/text/text_custom_emoji.h"

namespace KomaruColorSwatch {

[[nodiscard]] TextWithEntities AddSwatches(TextWithEntities text);

// The color the text ends with, invalid when it ends with something else.
[[nodiscard]] QColor TrailingColor(const QString &text);

// A rounded square of the color with a faint border of the text color.
void Paint(QPainter &p, QRect rect, QColor color, QColor textColor);

[[nodiscard]] bool IsSwatchData(QStringView data);
[[nodiscard]] std::unique_ptr<Ui::Text::CustomEmoji> MakeSwatch(
	QStringView data);

} // namespace KomaruColorSwatch
