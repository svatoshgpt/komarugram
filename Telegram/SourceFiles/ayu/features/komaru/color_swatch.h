// This is the source code of KomaruGram for Desktop.
#pragma once

#include "ui/text/text_custom_emoji.h"

namespace KomaruColorSwatch {

[[nodiscard]] TextWithEntities AddSwatches(TextWithEntities text);

[[nodiscard]] bool IsSwatchData(QStringView data);
[[nodiscard]] std::unique_ptr<Ui::Text::CustomEmoji> MakeSwatch(
	QStringView data);

} // namespace KomaruColorSwatch
