// This is the source code of KomaruGram for Desktop.
#include "ayu/ui/components/color_swatch.h"

#include "ui/painter.h"
#include "ui/text/text_custom_emoji.h"
#include "styles/style_ayu_styles.h"

#include <QtCore/QRegularExpression>

#include <algorithm>

namespace AyuUi {
namespace {

constexpr auto kPrefix = QStringView(u"komaru-color:");
constexpr auto kPlaceholder = QChar(0x200B);

[[nodiscard]] QString ColorData(const QColor &color) {
	return kPrefix.toString() + QString::asprintf(
		"%02x%02x%02x%02x",
		color.red(),
		color.green(),
		color.blue(),
		color.alpha());
}

class ColorSwatch final : public Ui::Text::CustomEmoji {
public:
	explicit ColorSwatch(QColor color) : _color(color) {
	}

	int width() override {
		return st::colorSwatchSkip + st::colorSwatchSize + st::colorSwatchSkip;
	}

	QString entityData() override {
		return ColorData(_color);
	}

	Ui::Text::CustomEmojiSemantics semantics() override {
		return {
			.isEmoji = false,
			.isRealCustomEmoji = false,
			.exportEntity = false,
			.unloadPersistentAnimation = false,
			.allowCustomEmojiClick = false,
		};
	}

	void paint(QPainter &p, const Context &context) override {
		const auto size = st::colorSwatchSize;
		const auto top = (Ui::Emoji::GetCustomSizeNormal() - size) / 2;
		const auto rect = QRect(
			context.position + QPoint(st::colorSwatchSkip, top),
			QSize(size, size));
		auto hq = PainterHighQualityEnabler(p);
		auto border = QColor(context.textColor);
		border.setAlphaF(border.alphaF() * 0.35);
		auto pen = QPen(border);
		pen.setWidthF(st::colorSwatchBorder);
		p.setPen(pen);
		p.setBrush(_color);
		const auto radius = st::colorSwatchRadius;
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
	QColor _color;

};

[[nodiscard]] QColor ParseHex(QStringView hex) {
	auto expanded = hex.toString();
	if (expanded.size() == 3) {
		expanded = QString()
			+ expanded[0] + expanded[0]
			+ expanded[1] + expanded[1]
			+ expanded[2] + expanded[2];
	}
	auto ok = false;
	const auto value = expanded.toUInt(&ok, 16);
	if (!ok) {
		return QColor();
	}
	if (expanded.size() == 8) {
		return QColor(
			(value >> 24) & 0xFF,
			(value >> 16) & 0xFF,
			(value >> 8) & 0xFF,
			value & 0xFF);
	}
	return QColor((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
}

[[nodiscard]] bool InsideBlockedEntity(
		const EntitiesInText &entities,
		int from,
		int till) {
	for (const auto &entity : entities) {
		const auto type = entity.type();
		const auto blocked = (type == EntityType::CustomEmoji)
			|| (type == EntityType::Url)
			|| (type == EntityType::Email);
		if (blocked
			&& entity.offset() < till
			&& entity.offset() + entity.length() > from) {
			return true;
		}
	}
	return false;
}

} // namespace

TextWithEntities AddColorSwatches(TextWithEntities text) {
	static const auto expression = QRegularExpression(
		u"(?<![\\w#&/])#([0-9a-fA-F]{8}|[0-9a-fA-F]{6}|[0-9a-fA-F]{3})(?!\\w)"_q);
	auto positions = std::vector<std::pair<int, QColor>>();
	auto i = expression.globalMatch(text.text);
	while (i.hasNext()) {
		const auto match = i.next();
		const auto from = int(match.capturedStart(0));
		const auto till = int(match.capturedEnd(0));
		if (InsideBlockedEntity(text.entities, from, till)) {
			continue;
		}
		const auto color = ParseHex(match.capturedView(1));
		if (color.isValid()) {
			positions.emplace_back(till, color);
		}
	}
	// Insert from the end so the earlier positions stay valid. Entities
	// that start at or after an insertion move right; those that span it
	// grow by the inserted placeholder.
	for (auto j = positions.rbegin(); j != positions.rend(); ++j) {
		const auto at = j->first;
		text.text.insert(at, kPlaceholder);
		for (auto &entity : text.entities) {
			if (entity.offset() >= at) {
				entity.shiftRight(1);
			} else if (entity.offset() + entity.length() > at) {
				entity = EntityInText(
					entity.type(),
					entity.offset(),
					entity.length() + 1,
					entity.data());
			}
		}
		text.entities.push_back(EntityInText(
			EntityType::CustomEmoji,
			at,
			1,
			ColorData(j->second)));
	}
	if (!positions.empty()) {
		std::stable_sort(
			text.entities.begin(),
			text.entities.end(),
			[](const EntityInText &a, const EntityInText &b) {
				return a.offset() < b.offset();
			});
	}
	return text;
}

Ui::Text::MarkedContext WithColorSwatches(Ui::Text::MarkedContext context) {
	auto base = std::move(context.customEmojiFactory);
	context.customEmojiFactory = [base = std::move(base)](
			QStringView data,
			const Ui::Text::MarkedContext &context)
	-> std::unique_ptr<Ui::Text::CustomEmoji> {
		if (data.startsWith(kPrefix)) {
			const auto color = ParseHex(data.mid(kPrefix.size()));
			return color.isValid()
				? std::make_unique<ColorSwatch>(color)
				: nullptr;
		}
		return base ? base(data, context) : nullptr;
	};
	return context;
}

} // namespace AyuUi
