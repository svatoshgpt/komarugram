// This is the source code of KomaruGram for Desktop.
#include "ayu/features/komaru/send_metadata.h"

#include <QtCore/QFile>
#include <QtCore/QRegularExpression>

namespace KomaruMetadata {
namespace {

constexpr auto kMaxMoovSize = int64(64 * 1024 * 1024);

class Tiff {
public:
	Tiff(const QByteArray &data, int start, int64 fileOffset, Ranges &out)
	: _data(data)
	, _start(start)
	, _fileOffset(fileOffset)
	, _out(out) {
		if (_start + 8 > _data.size()) {
			return;
		}
		const auto order = _data.mid(_start, 2);
		if (order == "II") {
			_little = true;
		} else if (order != "MM") {
			return;
		}
		_valid = true;
	}

	void scrub() {
		if (!_valid) {
			return;
		}
		const auto ifd0 = u32(4);
		forEntry(ifd0, [&](int entry, int tag) {
			switch (tag) {
			case 0x010F: // Make
			case 0x0110: // Model
			case 0x0131: // Software
			case 0x0132: // DateTime
				zeroValue(entry);
				break;
			case 0x8769: // Exif IFD
				scrubExif(u32(entry + 8 - _start));
				break;
			case 0x8825: // GPS IFD
				scrubGps(u32(entry + 8 - _start));
				break;
			}
		});
	}

private:
	[[nodiscard]] bool has(int64 at, int64 size) const {
		return at >= 0 && size >= 0 && (_start + at + size) <= _data.size();
	}
	[[nodiscard]] uint32 byte(int64 at) const {
		return uchar(_data[int(_start + at)]);
	}
	[[nodiscard]] uint32 u16(int64 at) const {
		if (!has(at, 2)) {
			return 0;
		}
		return _little
			? (byte(at) | (byte(at + 1) << 8))
			: ((byte(at) << 8) | byte(at + 1));
	}
	[[nodiscard]] uint32 u32(int64 at) const {
		if (!has(at, 4)) {
			return 0;
		}
		return _little
			? (u16(at) | (u16(at + 2) << 16))
			: ((u16(at) << 16) | u16(at + 2));
	}

	// Calls with the entry's position in _data and its tag.
	template <typename Callback>
	void forEntry(uint32 ifd, Callback &&callback) {
		if (!ifd || !has(ifd, 2)) {
			return;
		}
		const auto count = u16(ifd);
		for (auto i = 0; i != int(count); ++i) {
			const auto at = int64(ifd) + 2 + i * 12;
			if (!has(at, 12)) {
				return;
			}
			callback(int(_start + at), int(u16(at)));
		}
	}

	void zero(int64 tiffAt, int64 size) {
		if (size > 0 && has(tiffAt, size)) {
			_out.emplace_back(_fileOffset + _start + tiffAt, size);
		}
	}

	void zeroValue(int entry) {
		const auto at = entry - _start;
		const auto type = u16(at + 2);
		const auto count = int64(u32(at + 4));
		static constexpr int kSizes[] = { 0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8 };
		const auto unit = (type < std::size(kSizes)) ? kSizes[type] : 0;
		const auto size = unit * count;
		if (size <= 0) {
			return;
		} else if (size <= 4) {
			zero(at + 8, size);
		} else {
			zero(u32(at + 8), size);
		}
	}

	void scrubExif(uint32 ifd) {
		forEntry(ifd, [&](int entry, int tag) {
			switch (tag) {
			case 0x9003: // DateTimeOriginal
			case 0x9004: // DateTimeDigitized
			case 0xA430: // CameraOwnerName
			case 0xA431: // BodySerialNumber
			case 0xA434: // LensModel
			case 0xA435: // LensSerialNumber
				zeroValue(entry);
				break;
			}
		});
	}

	void scrubGps(uint32 ifd) {
		if (!ifd || !has(ifd, 2)) {
			return;
		}
		forEntry(ifd, [&](int entry, int) {
			zeroValue(entry);
		});
		// An empty directory is left: no entries, no next one.
		zero(ifd, 2 + int64(u16(ifd)) * 12 + 4);
	}

	const QByteArray &_data;
	const int _start = 0;
	const int64 _fileOffset = 0;
	Ranges &_out;
	bool _little = false;
	bool _valid = false;

};

void FindJpeg(QFile &file, Ranges &out) {
	auto offset = int64(2);
	while (file.seek(offset)) {
		const auto header = file.read(4);
		if (header.size() < 4 || uchar(header[0]) != 0xFF) {
			return;
		}
		const auto marker = uchar(header[1]);
		if (marker == 0xDA || marker == 0xD9) {
			return;
		}
		const auto length = (uchar(header[2]) << 8) | uchar(header[3]);
		if (length < 2) {
			return;
		}
		if (marker == 0xE1) {
			const auto data = file.read(length - 2);
			if (data.startsWith(QByteArray("Exif\0\0", 6))) {
				Tiff(data, 6, offset + 4, out).scrub();
			}
		}
		offset += 2 + length;
	}
}

void FindMp4(QFile &file, Ranges &out) {
	auto offset = int64(0);
	const auto total = file.size();
	while (offset + 8 <= total && file.seek(offset)) {
		const auto header = file.read(16);
		if (header.size() < 8) {
			return;
		}
		auto size = int64((uchar(header[0]) << 24)
			| (uchar(header[1]) << 16)
			| (uchar(header[2]) << 8)
			| uchar(header[3]));
		auto skip = int64(8);
		if (size == 1 && header.size() >= 16) {
			size = 0;
			for (auto i = 8; i != 16; ++i) {
				size = (size << 8) | uchar(header[i]);
			}
			skip = 16;
		} else if (size == 0) {
			size = total - offset;
		}
		if (size < skip) {
			return;
		}
		if (header.mid(4, 4) == "moov" && size <= kMaxMoovSize) {
			file.seek(offset + skip);
			const auto body = file.read(size - skip);
			const auto bodyOffset = offset + skip;
			// "©xyz" holds the location of Android and older iPhones.
			const auto xyz = QByteArray("\xA9xyz", 4);
			for (auto i = body.indexOf(xyz); i >= 4; i = body.indexOf(xyz, i + 4)) {
				const auto box = int64((uchar(body[i - 4]) << 24)
					| (uchar(body[i - 3]) << 16)
					| (uchar(body[i - 2]) << 8)
					| uchar(body[i - 1]));
				if (box > 8 && i - 4 + box <= body.size()) {
					out.emplace_back(bodyOffset + i + 4, box - 8);
				}
			}
			// Apple keeps it as an ISO 6709 string among its metadata keys.
			static const auto iso = QRegularExpression(
				u"[+-]\\d{1,2}\\.\\d+[+-]\\d{1,3}\\.\\d+([+-]\\d+(\\.\\d+)?)?(CRS[^/]*)?/"_q);
			const auto latin = QString::fromLatin1(body);
			auto matches = iso.globalMatch(latin);
			while (matches.hasNext()) {
				const auto match = matches.next();
				out.emplace_back(
					bodyOffset + match.capturedStart(),
					match.capturedLength());
			}
			return;
		}
		offset += size;
	}
}

} // namespace

Ranges Find(const QString &path) {
	auto result = Ranges();
	auto file = QFile(path);
	if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
		return result;
	}
	const auto head = file.read(12);
	if (head.size() >= 2 && uchar(head[0]) == 0xFF && uchar(head[1]) == 0xD8) {
		FindJpeg(file, result);
	} else if (head.size() >= 8 && head.mid(4, 4) == "ftyp") {
		FindMp4(file, result);
	}
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
