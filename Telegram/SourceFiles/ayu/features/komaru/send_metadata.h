// This is the source code of KomaruGram for Desktop.
#pragma once

namespace KomaruMetadata {

// Byte ranges of a file, as offset and size, that tell where, when and with what it was taken.
using Ranges = std::vector<std::pair<int64, int64>>;

// JPEG EXIF (GPS, camera, dates, serials) and MP4 / MOV locations.
[[nodiscard]] Ranges Find(const QString &path);

// Zeroes what of the ranges falls into a part read at the offset.
void Scrub(QByteArray &part, int64 offset, const Ranges &ranges);

} // namespace KomaruMetadata
