// This is the source code of KomaruGram for Desktop.
#include "ayu/features/komaru/color_swatch.h"

#include "ui/painter.h"
#include "styles/style_basic.h"

#include <komaru_core.h>

namespace KomaruColorSwatch {
namespace {

const auto kPrefix = u"komaru-color:"_q;

class Swatch final : public Ui::Text::CustomEmoji {
public:
	Swatch(QString data, QColor color)
	: _data(std::move(data))
	, _color(color) {
	}

	int width() override {
		return st::emojiSize + 2 * st::emojiPadding;
	}
	QString entityData() override {
		return _data;
	}
	Ui::Text::CustomEmojiSemantics semantics() override {
		return {
			.isEmoji = false,
			.isRealCustomEmoji = false,
			.exportEntity = false,
			.allowCustomEmojiClick = false,
		};
	}
	void paint(QPainter &p, const Context &context) override {
		const auto full = Ui::Text::AdjustCustomEmojiSize(st::emojiSize);
		const auto size = full * 4 / 5;
		const auto skip = (full - size) / 2;
		const auto rect = QRect(
			context.position + QPoint(skip, skip),
			QSize(size, size));
		KomaruColorSwatch::Paint(p, rect, _color, context.textColor);
	}
	void unload() override {
	}
	bool ready() override {
		return true;
	}
	bool readyInDefaultState() override {
		return true;
	}

private:
	QString _data;
	QColor _color;

};

// Links, code and the like keep their text untouched.
[[nodiscard]] bool ExtendsOverSwatch(EntityType type) {
	switch (type) {
	case EntityType::Bold:
	case EntityType::Semibold:
	case EntityType::Italic:
	case EntityType::Underline:
	case EntityType::StrikeOut:
	case EntityType::Blockquote:
	case EntityType::Spoiler:
	case EntityType::CustomUrl:
		return true;
	default:
		return false;
	}
}

[[nodiscard]] QColor ParseHex(QStringView hex) {
	if (hex.size() == 3) {
		auto full = QString();
		for (const auto ch : hex) {
			full.append(ch).append(ch);
		}
		return QColor(u"#"_q + full);
	}
	return QColor(u"#"_q + hex.toString());
}

} // namespace

TextWithEntities AddSwatches(TextWithEntities text) {
	auto positions = std::vector<std::pair<int, QString>>();
	struct Context {
		const QString &text;
		std::vector<std::pair<int, QString>> &positions;
	};
	auto context = Context{ text.text, positions };
	komaru_color_find(text.text.utf16(), text.text.size(), [](
			void *context,
			size_t end,
			size_t digits) {
		const auto data = static_cast<Context*>(context);
		data->positions.emplace_back(
			int(end),
			data->text.mid(int(end - digits), int(digits)));
	}, &context);
	if (positions.empty()) {
		return text;
	}
	// From the end, so earlier positions stay valid.
	for (auto j = positions.rbegin(); j != positions.rend(); ++j) {
		const auto at = j->first;
		const auto inside = [&](const EntityInText &entity) {
			return (entity.offset() < at)
				&& (entity.offset() + entity.length() > at);
		};
		const auto blocked = ranges::any_of(text.entities, [&](
				const EntityInText &entity) {
			return inside(entity) && !ExtendsOverSwatch(entity.type());
		});
		if (blocked) {
			continue;
		}
		for (auto &entity : text.entities) {
			if (entity.offset() >= at) {
				entity.shiftRight(1);
			} else if (inside(entity)) {
				entity.shrinkFromRight(-1);
			}
		}
		text.text.insert(at, QChar(' '));
		text.entities.push_back(EntityInText(
			EntityType::CustomEmoji,
			at,
			1,
			kPrefix + j->second.toLower()));
	}
	std::stable_sort(
		text.entities.begin(),
		text.entities.end(),
		[](const EntityInText &a, const EntityInText &b) {
			return a.offset() < b.offset();
		});
	return text;
}

QColor TrailingColor(const QString &text) {
	auto size = text.size();
	while (size > 0 && text[size - 1].isSpace()) {
		--size;
	}
	struct Found {
		size_t end = 0;
		size_t digits = 0;
	};
	auto found = Found();
	komaru_color_find(text.utf16(), size, [](
			void *context,
			size_t end,
			size_t digits) {
		*static_cast<Found*>(context) = { end, digits };
	}, &found);
	return (found.digits && qsizetype(found.end) == size)
		? ParseHex(QStringView(text).mid(
			qsizetype(found.end - found.digits),
			qsizetype(found.digits)))
		: QColor();
}

void Paint(QPainter &p, QRect rect, QColor color, QColor textColor) {
	const auto radius = rect.width() / 4.;
	auto hq = PainterHighQualityEnabler(p);
	auto border = textColor;
	border.setAlphaF(0.3);
	p.setPen(QPen(border, style::ConvertScaleExact(1.)));
	p.setBrush(color);
	p.drawRoundedRect(rect, radius, radius);
}

bool IsSwatchData(QStringView data) {
	return data.startsWith(kPrefix);
}

std::unique_ptr<Ui::Text::CustomEmoji> MakeSwatch(QStringView data) {
	const auto color = ParseHex(data.mid(kPrefix.size()));
	return color.isValid()
		? std::make_unique<Swatch>(data.toString(), color)
		: nullptr;
}

} // namespace KomaruColorSwatch
