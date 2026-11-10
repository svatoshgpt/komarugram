// This is the source code of KomaruGram for Desktop.
#include "ayu/features/komaru/color_swatch.h"

#include "ui/painter.h"
#include "styles/style_basic.h"

#include <QtCore/QRegularExpression>

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
		const auto radius = size / 4.;
		auto hq = PainterHighQualityEnabler(p);
		auto border = QColor(context.textColor);
		border.setAlphaF(0.3);
		p.setPen(QPen(border, style::ConvertScaleExact(1.)));
		p.setBrush(_color);
		p.drawRoundedRect(rect, radius, radius);
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
	static const auto regex = QRegularExpression(
		u"(?<![\\w#])#([0-9a-fA-F]{6}|[0-9a-fA-F]{3})(?![\\w])"_q);
	auto positions = std::vector<std::pair<int, QString>>();
	auto i = regex.globalMatch(text.text);
	while (i.hasNext()) {
		const auto match = i.next();
		positions.emplace_back(match.capturedEnd(), match.captured(1));
	}
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
