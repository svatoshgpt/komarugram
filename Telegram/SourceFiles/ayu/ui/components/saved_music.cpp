// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/components/saved_music.h"

#include "ayu/ayu_settings.h"
#include "ayu/ui/utils/color_utils.h"
#include "ayu/ui/utils/itunes_search.h"
#include "ayu/ui/utils/palette.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "info/profile/info_profile_music_button.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "styles/palette.h"
#include "styles/style_info.h"
#include "styles/style_widgets.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/image/image.h"
#include "ui/widgets/labels.h"
#include "window/themes/window_theme.h"

#include <QSvgRenderer>
#include <QtGui/QPainterPath>
#include <algorithm>

namespace Info::Profile {

namespace {

QColor performerColor(255, 255, 255, 192); // white 75%
QColor metaColor(255, 255, 255, 148); // white 58%

[[nodiscard]] QString FormatPlatformLabel(const QString &platform) {
	const auto upper = platform.trimmed().toUpper();
	if (upper == u"SPOTIFY"_q) {
		return u"Spotify"_q;
	} else if (upper == u"LAST_FM"_q || upper == u"LASTFM"_q) {
		return u"Last.fm"_q;
	} else if (upper == u"STATS_FM"_q || upper == u"STATSFM"_q) {
		return u"Stats.fm"_q;
	} else if (upper == u"APPLE_MUSIC"_q || upper == u"APPLE"_q) {
		return u"Apple Music"_q;
	} else if (upper == u"YANDEX"_q || upper == u"YANDEX_MUSIC"_q) {
		return u"Yandex Music"_q;
	} else if (upper == u"TELEGRAM"_q) {
		return u"Telegram"_q;
	}
	return QString();
}

void DrawMusicNote(
		QPainter &p,
		const QPointF &center,
		qreal scale,
		qreal angleDeg,
		const QColor &color) {
	p.save();
	p.translate(center);
	p.rotate(angleDeg);
	p.scale(scale, scale);
	p.setPen(Qt::NoPen);
	p.setBrush(color);

	// Classic beamed eighth note silhouette.
	p.drawEllipse(QPointF(-5.5, 5.5), 3.4, 2.5);
	p.drawEllipse(QPointF(4.5, 3.8), 3.4, 2.5);
	p.drawRoundedRect(QRectF(-3.1, -7.5, 2.0, 13.0), 1.0, 1.0);
	p.drawRoundedRect(QRectF(6.9, -9.2, 2.0, 13.0), 1.0, 1.0);

	auto beam = QPainterPath();
	beam.moveTo(-3.1, -7.5);
	beam.lineTo(8.9, -9.8);
	beam.lineTo(8.9, -6.4);
	beam.lineTo(-3.1, -4.1);
	beam.closeSubpath();
	p.drawPath(beam);
	p.restore();
}

struct NowPlayingMetrics {
	int padX = 0;
	int padY = 0;
	int gap = 0;
	int coverSize = 0;
	int btnSize = 0;
	int radius = 0;
	int innerRadius = 0;
};

[[nodiscard]] NowPlayingMetrics ComputeNowPlayingMetrics(int width, int baseRadius) {
	const auto compact = (width > 0 && width < style::ConvertScale(300));
	const auto wide = (width >= style::ConvertScale(420));
	const auto padX = style::ConvertScale(compact ? 10 : wide ? 14 : 12);
	const auto padY = style::ConvertScale(compact ? 10 : wide ? 14 : 12);
	const auto gap = style::ConvertScale(compact ? 10 : wide ? 14 : 12);
	const auto coverSize = style::ConvertScale(compact ? 48 : wide ? 64 : 56);
	const auto btnSize = style::ConvertScale(compact ? 32 : wide ? 40 : 36);
	const auto radius = (baseRadius > 0) ? baseRadius : style::ConvertScale(16);
	const auto innerRadius = std::max(
		radius - padX,
		style::ConvertScale(compact ? 8 : 10));
	return {
		.padX = padX,
		.padY = padY,
		.gap = gap,
		.coverSize = coverSize,
		.btnSize = btnSize,
		.radius = radius,
		.innerRadius = innerRadius,
	};
}

void DrawNowPlayingPattern(
		QPainter &p,
		const QRect &rect,
		const QColor &accent,
		qreal alphaScale) {
	struct Stamp {
		qreal rx;
		qreal ry;
		qreal scale;
		qreal angle;
		qreal alpha;
	};
	constexpr Stamp kStamps[] = {
		{ 0.58, 0.24, 0.72, -16.0, 0.11 },
		{ 0.72, 0.74, 0.85,  14.0, 0.13 },
		{ 0.84, 0.22, 0.92, -10.0, 0.15 },
		{ 0.94, 0.66, 0.78,  18.0, 0.12 },
		{ 0.64, 0.52, 0.62,  -8.0, 0.08 },
	};
	for (const auto &s : kStamps) {
		auto c = accent;
		c.setAlphaF(std::clamp(s.alpha * alphaScale, 0.0, 1.0));
		DrawMusicNote(
			p,
			QPointF(rect.width() * s.rx, rect.height() * s.ry),
			s.scale,
			s.angle,
			c);
	}
}

QRgb AdjustHsl(QRgb color, float luminance, float saturation = -1.0f) {
	auto hsl = Ayu::Ui::ColorUtils::colorToHSL(color);

	if (saturation > 0.0f) {
		hsl[1] = std::min(hsl[1] * saturation, 1.0f);
	}

	hsl[2] = std::min(hsl[2] * luminance, 1.0f);

	return Ayu::Ui::ColorUtils::HSLToRGB(hsl);
}

[[nodiscard]] QColor ComputeAccentColor(QColor bg) {
	const auto hsl = Ayu::Ui::ColorUtils::colorToHSL(bg.rgb());
	const auto lum = hsl[2];
	const auto targetLum = (lum <= 0.25f)
		? 2.0f
		: (lum <= 0.50f)
		? 1.5f
		: (lum <= 0.75f)
		? 1.0f
		: 0.5f;
	return QColor::fromRgb(AdjustHsl(bg.rgb(), targetLum));
}

QRgb BlendARGB(QRgb color1, QRgb color2, float ratio) {
	const auto inverseRatio = 1.0f - ratio;
	const auto r = static_cast<int>(qRed(color1) * inverseRatio + qRed(color2) * ratio);
	const auto g = static_cast<int>(qGreen(color1) * inverseRatio + qGreen(color2) * ratio);
	const auto b = static_cast<int>(qBlue(color1) * inverseRatio + qBlue(color2) * ratio);
	const auto a = static_cast<int>(qAlpha(color1) * inverseRatio + qAlpha(color2) * ratio);
	return qRgba(r, g, b, a);
}

QColor GetNoCoverBgColor(std::optional<QColor> overrideBg) {
	if (overrideBg) {
		return Ui::BlendColors(
			*overrideBg,
			Qt::black,
			st::infoProfileTopBarActionButtonBgOpacity);
	}

	return st::shadowFg->c;
}

struct Cover
{
	QPixmap pixToDraw;
	QPixmap pixToBg;
	bool noCover;
};

QPixmap MakeNoCoverImage(const QSize &size) {
	static QPixmap result;
	static auto resultTheme = Window::Theme::Background()->id();
	if (!result.isNull() && result.size() == size && resultTheme == Window::Theme::Background()->id()) {
		return result;
	}
	resultTheme = Window::Theme::Background()->id();

	auto image = QImage(size, QImage::Format_ARGB32);
	{
		auto p = Painter(&image);
		auto hq = PainterHighQualityEnabler(p);

		const auto bgColor = Window::Theme::IsNightMode()
								 ? st::windowBoldFg->c.darker()
								 : st::windowBoldFg->c.lighter();
		image.fill(bgColor);

		auto svgIcon = QSvgRenderer(u":/gui/icons/ayu/nocover.svg"_q);
		p.setPen(st::windowBoldFg->p);
		svgIcon.render(&p, QRect(0, 0, size.width(), size.height()));
	}
	const auto img = Image(std::move(image));
	result = img.pix(size, Images::PrepareArgs{.options = Images::Option::RoundLarge});
	return result;
}

} // namespace

Cover GetCurrentCover(
	const std::shared_ptr<Data::DocumentMedia> &dataMedia,
	const QSize &size) {
	if (!dataMedia) {
		return {
			.pixToDraw = MakeNoCoverImage(size),
			.pixToBg = MakeNoCoverImage(size),
			.noCover = true
		};
	}

	auto cover = QPixmap();
	const auto scaled = [&](not_null<Image*> image)
	{
		const auto aspectRatio = Qt::KeepAspectRatioByExpanding;
		const auto targetSize = size * style::DevicePixelRatio();
		return image->size().scaled(targetSize, aspectRatio);
	};
	const auto args = Images::PrepareArgs{
		.options = Images::Option::RoundLarge,
		.outer = size,
	};
	if (const auto normal = dataMedia->thumbnail()) {
		return {
			.pixToDraw = normal->pixNoCache(scaled(normal), args),
			.pixToBg = normal->pixNoCache(),
			.noCover = false
		};
	}

	return {
		.pixToDraw = MakeNoCoverImage(size),
		.pixToBg = MakeNoCoverImage(size),
		.noCover = true
	};
}

std::optional<QRgb> ExtractColorFromCover(const QPixmap &cover) {
	const auto palette = Ayu::Ui::Palette::from(cover).generate();

	const auto *swatch = palette.darkVibrantSwatch();
	if (!swatch) {
		swatch = palette.mutedSwatch();
	}
	if (!swatch) {
		swatch = palette.darkMutedSwatch();
	}
	if (!swatch) {
		swatch = palette.dominantSwatch();
	}

	if (!swatch) {
		return std::nullopt;
	}

	const auto extractedColor = swatch->rgb();

	constexpr auto whiteColor = qRgb(255, 255, 255);
	const auto contrast = Ayu::Ui::ColorUtils::calculateContrast(whiteColor, extractedColor);

	auto adjustedColor = extractedColor;
	if (contrast > 15.0f) {
		adjustedColor = AdjustHsl(extractedColor, 2.0f);
	} else if (contrast < 10.0f) {
		adjustedColor = AdjustHsl(extractedColor, 0.5f);
	}

	if (Ayu::Ui::ColorUtils::calculateContrast(whiteColor, adjustedColor) < 3.0f) {
		adjustedColor = BlendARGB(adjustedColor, qRgb(0, 0, 0), 0.3f);
	}

	return adjustedColor;
}

AyuMusicButton::AyuMusicButton(
	QWidget *parent,
	MusicButtonData data,
	std::optional<QColor> overrideBg,
	Fn<void()> handler)
	: RippleButton(parent, st::infoMusicButtonRipple)
	  , _performer(std::make_unique<Ui::FlatLabel>(
		  this,
		  data.performer,
		  st::infoMusicButtonPerformer))
	  , _title(std::make_unique<Ui::FlatLabel>(
		  this,
		  data.title,
		  st::infoMusicButtonTitle))
	  , _meta(std::make_unique<Ui::FlatLabel>(
		  this,
		  QString(),
		  st::infoMusicButtonPerformer))
	  , _mediaView(data.mediaView)
	  , _overrideBg(overrideBg) {
	_performerText = data.performer;
	_titleText = data.title;
	_meta->hide();
	rpl::combine(
		_title->naturalWidthValue(),
		_performer->naturalWidthValue(),
		_meta->naturalWidthValue()
	) | rpl::on_next([=]
							 {
								 resizeToWidth(widthNoMargins());
							 },
							 lifetime());

	_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	_performer->setAttribute(Qt::WA_TransparentForMouseEvents);
	_meta->setAttribute(Qt::WA_TransparentForMouseEvents);

	style::PaletteChanged(
	) | rpl::on_next([=] {
		if (_currentCover) {
			makeCover();
		}
	}, lifetime());

	downloadAndMakeCover(data.msgId);

	setClickedCallback(std::move(handler));
}

AyuMusicButton::~AyuMusicButton() = default;

void AyuMusicButton::updateData(MusicButtonData data) {
	_performer->setText(data.performer);
	_title->setText(data.title);
	_performerText = data.performer;
	_titleText = data.title;
	_mediaView = data.mediaView;
	downloadAndMakeCover(data.msgId);

	resizeToWidth(widthNoMargins());
}

void AyuMusicButton::setPadding(QMargins padding) {
	_padding = padding;
	resizeToWidth(widthNoMargins());
	update();
}

void AyuMusicButton::setCornerRadius(int radius) {
	_radius = radius;
	update();
}

void AyuMusicButton::setPlayIconShown(bool shown) {
	_playIcon = shown;
	resizeToWidth(widthNoMargins());
	update();
}

void AyuMusicButton::setThemeColors(bool theme) {
	if (_themeColors == theme) {
		return;
	}
	_themeColors = theme;
	if (_currentCover) {
		makeCover();
	}
}

void AyuMusicButton::setNowPlayingCard(bool enabled) {
	if (_nowPlayingCard == enabled) {
		return;
	}
	_nowPlayingCard = enabled;
	if (_currentCover) {
		makeCover();
	}
	resizeToWidth(widthNoMargins());
	update();
}

void AyuMusicButton::setMeta(const QString &album, const QString &platform) {
	const auto cleanAlbum = (album.trimmed().compare(_titleText, Qt::CaseInsensitive) != 0)
		? album.trimmed()
		: QString();
	const auto cleanPlatform = FormatPlatformLabel(platform);
	auto parts = QStringList();
	if (!cleanAlbum.isEmpty()) {
		parts.push_back(cleanAlbum);
	}
	if (!cleanPlatform.isEmpty()) {
		parts.push_back(cleanPlatform);
	}
	const auto next = parts.join(u" · "_q);
	if (_metaText == next) {
		return;
	}
	_metaText = next;
	_meta->setText(_metaText);
	_meta->setVisible(_nowPlayingCard && !_metaText.isEmpty());
	resizeToWidth(widthNoMargins());
	update();
}

void AyuMusicButton::setExternalCover(QImage image) {
	_externalCover = std::move(image);
}

void AyuMusicButton::downloadAndMakeCover(FullMsgId msgId) {
	if (_mediaView && _mediaView->owner()->isSongWithCover() && !_mediaView->thumbnail()) {
		const auto settings = &_mediaView->owner()->session().settings().autoDownload();
		// Data::AutoDownload::Type::Music always returns false
		if (settings->shouldDownload(Data::AutoDownload::Source::User,
									 Data::AutoDownload::Type::File,
									 _mediaView->owner()->size)) {
			_mediaView->thumbnailWanted(Data::FileOrigin(msgId));
			_mediaView->owner()->owner().session().downloaderTaskFinished(
			) | rpl::take_while([=]
			{
				if (_mediaView->thumbnail()) {
					makeCover();
				}
				return !_mediaView->thumbnail();
			}) | rpl::start(lifetime());
			return;
		}
	}

	makeCover();
}

void AyuMusicButton::makeCover() {
	const auto weak = base::make_weak(this);
	crl::async(
		[=, mediaView = _mediaView, performerText = _performerText, titleText = _titleText, overrideBg = _overrideBg, external = _externalCover, themeColors = _themeColors, nowPlayingCard = _nowPlayingCard]()
		{
			const auto &settings = AyuSettings::getInstance();
			const auto adaptiveColor = !themeColors
				&& (nowPlayingCard || settings.adaptiveCoverColor());
			const auto &font = st::infoMusicButtonTitle.style.font;
			const auto skip = st::normalFont->spacew / 2;
			const auto size = nowPlayingCard
				? style::ConvertScale(64)
				: (font->height + skip + font->height);

			auto cover = GetCurrentCover(mediaView, QSize(size, size));

			if (external && !external->isNull()) {
				const auto img = Image(QImage(*external));
				const auto args = Images::PrepareArgs{
					.options = Images::Option::RoundLarge,
					.outer = QSize(size, size),
				};
				cover.pixToDraw = img.pix(QSize(size, size), args);
				cover.pixToBg = QPixmap::fromImage(*external);
				cover.noCover = false;
			} else if (cover.noCover && !external) {
				const auto pix = Ayu::Ui::Itunes::FetchCover(performerText, titleText, size);
				if (!pix.isNull()) {
					const auto img = Image(pix.toImage());
					const auto args = Images::PrepareArgs{
						.options = Images::Option::RoundLarge,
						.outer = QSize(size, size),
					};
					cover.pixToDraw = img.pix(QSize(size, size), args);
					cover.pixToBg = pix;
					cover.noCover = false;
				}
			}

			QColor bgColor;
			QColor accentColor;
			if (cover.noCover || !adaptiveColor) {
				bgColor = GetNoCoverBgColor(overrideBg);
			} else {
				if (const auto extractedColor = ExtractColorFromCover(cover.pixToBg)) {
					bgColor = QColor::fromRgb(*extractedColor);
					accentColor = ComputeAccentColor(bgColor);
				} else {
					// example: fully black image
					cover.noCover = true;
					bgColor = GetNoCoverBgColor(overrideBg);
				}
			}

			crl::on_main([weak, cover = std::move(cover), bgColor, accentColor, overrideBg, adaptiveColor]() mutable
			{
				const auto strong = weak.get();
				if (!strong) {
					return;
				}

				strong->_currentCover = {
					.pix = cover.pixToDraw,
					.bg = bgColor,
					.accent = accentColor,
					.noCover = cover.noCover,
				};

				const auto &cover2 = *strong->_currentCover;

				if (!cover2.noCover && adaptiveColor && !cover2.pix.isNull()) {
					strong->_title->setTextColorOverride(Qt::white);
					strong->_performer->setTextColorOverride(performerColor);
					strong->_meta->setTextColorOverride(metaColor);
				} else if (overrideBg) {
					strong->_title->setTextColorOverride(st::groupCallMembersFg->c);
					strong->_performer->setTextColorOverride(st::groupCallMembersFg->c);
					strong->_meta->setTextColorOverride(st::groupCallMembersFg->c);
				} else {
					strong->_title->setTextColorOverride(std::nullopt);
					strong->_performer->setTextColorOverride(std::nullopt);
					strong->_meta->setTextColorOverride(std::nullopt);
				}

				strong->repaint();
				strong->_title->repaint();
				strong->_performer->repaint();
				strong->_meta->repaint();

				strong->_onReady.fire({});
			});
		});
}

void AyuMusicButton::paintEvent(QPaintEvent *e) {
	if (!_currentCover) {
		return;
	}

	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto &font = st::infoMusicButtonTitle.style.font;
	const auto skip = st::normalFont->spacew / 2;
	const auto metrics = _nowPlayingCard
		? ComputeNowPlayingMetrics(width(), _radius)
		: NowPlayingMetrics{};
	const auto size = _nowPlayingCard
		? metrics.coverSize
		: (font->height + skip + font->height);
	const auto radius = _nowPlayingCard ? metrics.radius : _radius;

	if (radius > 0) {
		auto path = QPainterPath();
		path.addRoundedRect(rect(), radius, radius);
		p.setClipPath(path);
	}

	const auto &settings = AyuSettings::getInstance();
	const auto cover = _currentCover.value();
	const auto adaptive = !cover.noCover
		&& !_themeColors
		&& (_nowPlayingCard || settings.adaptiveCoverColor());
	if (!adaptive) {
		// Material 3 surface container adapting to current Light/Dark theme.
		p.fillRect(e->rect(), _overrideBg ? cover.bg : st::windowBgOver->c);
		if (_nowPlayingCard) {
			DrawNowPlayingPattern(p, rect(), st::windowSubTextFg->c, 0.55);
		}
		paintRipple(p, QPoint());
	} else {
		const auto center = _nowPlayingCard
			? QPointF(width(), height() / 2.0)
			: QPointF(rect().topRight());
		QRadialGradient gradient(center, width() * 2.0);
		gradient.setColorAt(0, cover.bg);
		gradient.setColorAt(1, QColor::fromRgb(AdjustHsl(cover.bg.rgb(), 1.45f)));
		p.fillRect(rect(), gradient);
		if (_nowPlayingCard) {
			const auto patternColor = cover.accent.isValid()
				? cover.accent
				: QColor(Qt::white);
			DrawNowPlayingPattern(p, rect(), patternColor, 1.0);
			paintRipple(p, QPoint());
		}
	}

	const auto padding = _padding.value_or(st::infoMusicButtonPadding);
	if (!cover.pix.isNull()) {
		const auto coverX = _nowPlayingCard ? metrics.padX : padding.left();
		const auto coverY = _nowPlayingCard
			? (height() - size) / 2
			: padding.top();
		const auto coverRect = QRect(coverX, coverY, size, size);
		p.drawPixmap(coverRect, cover.pix);
	}

	if (_playIcon) {
		const auto btnSize = _nowPlayingCard ? metrics.btnSize : size;
		// Material 3 tonal / accent circular play button.
		const auto fg = adaptive
			? QColor(Qt::white)
			: _overrideBg
			? st::groupCallMembersFg->c
			: st::windowBgActive->c;
		auto bg = (adaptive && _nowPlayingCard && cover.accent.isValid())
			? cover.accent
			: fg;
		if (!(adaptive && _nowPlayingCard && cover.accent.isValid())) {
			bg.setAlphaF(adaptive ? 0.22 : 0.16);
		}
		const auto rightInset = _nowPlayingCard ? metrics.padX : padding.left();
		const auto circle = QRectF(
			width() - rightInset - btnSize,
			(height() - btnSize) / 2.,
			btnSize,
			btnSize);
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		p.drawEllipse(circle);

		const auto unit = btnSize / 40.;
		const auto c = circle.center();
		auto triangle = QPainterPath();
		triangle.moveTo(c + QPointF(-4.5 * unit, -7. * unit));
		triangle.lineTo(c + QPointF(7. * unit, 0.));
		triangle.lineTo(c + QPointF(-4.5 * unit, 7. * unit));
		triangle.closeSubpath();
		auto pen = QPen(fg, 2.5 * unit);
		pen.setJoinStyle(Qt::RoundJoin);
		p.setPen(pen);
		p.setBrush(fg);
		p.drawPath(triangle);
	}
}

int AyuMusicButton::resizeGetHeight(int newWidth) {
	const auto padding = _padding.value_or(st::infoMusicButtonPadding);
	const auto &font = st::infoMusicButtonTitle.style.font;
	const auto &subFont = st::infoMusicButtonPerformer.style.font;
	const auto skip = st::normalFont->spacew / 2;

	if (_nowPlayingCard) {
		const auto metrics = ComputeNowPlayingMetrics(newWidth, _radius);
		const auto coverSize = metrics.coverSize;
		const auto btnSize = _playIcon ? metrics.btnSize : 0;
		const auto textLeft = metrics.padX + coverSize + metrics.gap;
		const auto rightReserve = metrics.padX + (btnSize ? (btnSize + metrics.gap) : 0);
		const auto available = std::max(0, newWidth - textLeft - rightReserve);

		const auto showMeta = !_metaText.isEmpty()
			&& (available >= style::ConvertScale(110));
		_meta->setVisible(showMeta);

		const auto lineGap = std::max(style::ConvertScale(2), skip / 2);
		const auto textHeight = font->height
			+ lineGap + subFont->height
			+ (showMeta ? (lineGap + subFont->height) : 0);
		const auto contentHeight = std::max(coverSize, textHeight);
		const auto totalHeight = metrics.padY + contentHeight + metrics.padY;
		const auto textTop = (totalHeight - textHeight) / 2;

		_title->resizeToNaturalWidth(available);
		_title->moveToLeft(textLeft, textTop);
		_performer->resizeToNaturalWidth(available);
		_performer->moveToLeft(textLeft, textTop + font->height + lineGap);
		if (showMeta) {
			_meta->resizeToNaturalWidth(available);
			_meta->moveToLeft(
				textLeft,
				textTop + font->height + lineGap + subFont->height + lineGap);
		}
		return totalHeight;
	}

	const auto top = padding.top();
	const auto coverSize = font->height + skip + font->height;

	const auto available = newWidth - padding.left() - padding.right() - coverSize
		- (_playIcon ? (coverSize + padding.left()) : 0);
	_title->resizeToNaturalWidth(available);
	_title->moveToLeft(padding.left() * 2 + coverSize, top);
	_performer->resizeToNaturalWidth(available);
	_performer->moveToLeft(padding.left() * 2 + coverSize, top + font->height + skip);

	return padding.top() + font->height + skip + font->height + padding.bottom();
}

} // namespace Info::Profile
