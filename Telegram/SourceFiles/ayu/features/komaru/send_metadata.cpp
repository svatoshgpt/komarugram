// This is the source code of KomaruGram for Desktop.
#include "ayu/features/komaru/send_metadata.h"

#include <komaru_core.h>

namespace KomaruMetadata {

Ranges Find(const QString &path) {
	auto result = Ranges();
	if (path.isEmpty()) {
		return result;
	}
	const auto utf = path.toUtf8();
	komaru_metadata_find(utf.constData(), utf.size(), [](
			void *context,
			int64_t offset,
			int64_t size) {
		static_cast<Ranges*>(context)->emplace_back(offset, size);
	}, &result);
	return result;
}

void Scrub(QByteArray &part, int64 offset, const Ranges &ranges) {
	const auto end = offset + part.size();
	for (const auto &[from, size] : ranges) {
		const auto a = std::max(from, offset);
		const auto b = std::min(from + size, end);
		if (a < b) {
			std::fill(
				part.begin() + (a - offset),
				part.begin() + (b - offset),
				char(0));
		}
	}
}

} // namespace KomaruMetadata
