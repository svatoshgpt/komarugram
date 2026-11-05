// This file is part of KomaruGram for Desktop, see https://github.com/svatoshgpt/komarugram
#include "ayu/ui/settings/settings_now_playing.h"

#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/features/komaru/now_playing.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_main.h"
#include "base/call_delayed.h"
#include "lang/lang_text_entity.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_ayu_icons.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"

namespace Settings {

using namespace Builder;
using namespace AyuBuilder;

namespace {

constexpr auto kSaveDelay = crl::time(1000);
constexpr auto kLastFmKeyLimit = 64;

[[nodiscard]] rpl::producer<TextWithEntities> SiteLink(
		rpl::producer<NowPlayingService> service) {
	return std::move(service) | rpl::map([](NowPlayingService value) {
		return tr::link(
			tr::ayu_NowPlayingOfficialSite(tr::now),
			KomaruNowPlaying::SiteUrl(value));
	});
}

not_null<Ui::InputField*> AddField(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> placeholder,
		const QString &value,
		int limit,
		Fn<void(QString)> save) {
	const auto style = Ui::AttachAsChild(container, [&] {
		auto result = st::settingsBio;
		result.textMargins.setRight(st::boxTextFont->spacew
			+ st::boxTextFont->width(QString::number(limit)));
		return result;
	}());
	const auto field = container->add(
		object_ptr<Ui::InputField>(
			container,
			*style,
			Ui::InputField::Mode::SingleLine,
			std::move(placeholder),
			value),
		st::settingsBioMargins);
	field->setMaxLength(limit);

	const auto countdown = Ui::CreateChild<Ui::FlatLabel>(
		container.get(),
		QString(),
		st::settingsBioCountdown);
	rpl::combine(
		field->geometryValue(),
		countdown->widthValue()
	) | rpl::on_next([=](QRect geometry, int width) {
		countdown->move(
			geometry.x() + geometry.width() - width,
			geometry.y() + style->textMargins.top());
	}, countdown->lifetime());

	const auto generation = Ui::CreateChild<int>(field);
	const auto commit = [=] {
		*generation = 0;
		save(field->getLastText().trimmed());
	};
	const auto updated = [=] {
		countdown->setText(QString::number(
			limit - int(field->getLastText().size())));
		const auto saved = ++*generation;
		base::call_delayed(kSaveDelay, field, [=] {
			if (*generation == saved) {
				commit();
			}
		});
	};
	countdown->setText(QString::number(limit - int(value.size())));
	field->changes() | rpl::on_next(updated, field->lifetime());
	field->submits() | rpl::on_next(commit, field->lifetime());
	container->lifetime().add([=] {
		if (*generation > 0) {
			save(field->getLastText().trimmed());
		}
	});
	return field;
}

void BuildService(SectionBuilder &builder) {
	builder.add([](const BuildContext &ctx) {
		v::match(ctx, [&](const WidgetContext &wctx) {
			const auto container = wctx.container;
			auto &settings = AyuSettings::getInstance();

			Ui::AddSubsectionTitle(
				container,
				tr::ayu_NowPlayingServiceHeader());
			const auto group = std::make_shared<
				Ui::RadioenumGroup<NowPlayingService>>(
					settings.nowPlayingService());
			const auto add = [&](NowPlayingService value, QString text) {
				container->add(
					object_ptr<Ui::Radioenum<NowPlayingService>>(
						container,
						group,
						value,
						text,
						st::settingsSendType),
					st::settingsSendTypePadding);
			};
			add(NowPlayingService::None, tr::ayu_NowPlayingNone(tr::now));
			add(NowPlayingService::LastFm, u"Last.fm"_q);
			add(NowPlayingService::StatsFm, u"Stats.fm"_q);
			group->setChangedCallback([](NowPlayingService value) {
				AyuSettings::getInstance().setNowPlayingService(value);
			});
			Ui::AddSkip(container);
			Ui::AddDividerText(container, tr::ayu_NowPlayingServiceAbout());

			const auto active = container->add(
				object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
					container,
					object_ptr<Ui::VerticalLayout>(container)));
			const auto inner = active->entity();
			Ui::AddSkip(inner);
			Ui::AddSubsectionTitle(
				inner,
				tr::ayu_NowPlayingUsernameHeader());
			AddField(
				inner,
				tr::ayu_NowPlayingUsernamePlaceholder(),
				settings.nowPlayingUsername(),
				KomaruNowPlaying::kUsernameLimit,
				[](QString value) {
					AyuSettings::getInstance().setNowPlayingUsername(value);
				});
			Ui::AddDividerText(
				inner,
				tr::ayu_NowPlayingUsernameAbout(
					lt_link,
					SiteLink(settings.nowPlayingServiceValue()),
					tr::marked));

			const auto lastFm = inner->add(
				object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
					inner,
					object_ptr<Ui::VerticalLayout>(inner)));
			const auto keys = lastFm->entity();
			Ui::AddSkip(keys);
			Ui::AddSubsectionTitle(keys, tr::ayu_NowPlayingApiKeyHeader());
			AddField(
				keys,
				tr::ayu_NowPlayingApiKeyPlaceholder(),
				settings.nowPlayingLastFmKey(),
				kLastFmKeyLimit,
				[](QString value) {
					AyuSettings::getInstance().setNowPlayingLastFmKey(value);
				});
			Ui::AddDividerText(
				keys,
				tr::ayu_NowPlayingApiKeyAbout(
					lt_link,
					rpl::single(tr::link(
						tr::ayu_NowPlayingOfficialSite(tr::now),
						u"https://www.last.fm/api/account/create"_q)),
					tr::marked));

			active->toggleOn(settings.nowPlayingServiceValue(
			) | rpl::map(rpl::mappers::_1 != NowPlayingService::None));
			active->finishAnimating();
			lastFm->toggleOn(settings.nowPlayingServiceValue(
			) | rpl::map(rpl::mappers::_1 == NowPlayingService::LastFm));
			lastFm->finishAnimating();
		}, [&](const SearchContext &sctx) {
			sctx.entries->push_back({
				.id = u"ayu/nowPlaying"_q,
				.title = tr::ayu_NowPlaying(tr::now),
				.section = sctx.sectionId,
			});
		});
	});
}

void BuildPublic(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSkip();
	builder.addSubsectionTitle(tr::ayu_NowPlayingPublicHeader());
	ayu.addSettingToggle({
		.id = u"ayu/nowPlayingProfileMusic"_q,
		.title = tr::ayu_NowPlayingProfileMusic(),
		.getter = &AyuSettings::nowPlayingProfileMusic,
		.setter = &AyuSettings::setNowPlayingProfileMusic,
	});
	ayu.addSettingToggle({
		.id = u"ayu/nowPlayingBio"_q,
		.title = tr::ayu_NowPlayingBio(),
		.getter = &AyuSettings::nowPlayingBio,
		.setter = &AyuSettings::setNowPlayingBio,
	});
	builder.addSkip();
	builder.addDividerText(tr::ayu_NowPlayingPublicAbout());
}

const auto kMeta = BuildHelper({
	.id = AyuNowPlaying::Id(),
	.parentId = AyuMain::Id(),
	.title = &tr::ayu_NowPlaying,
	.icon = &st::ayuNowPlayingIcon,
}, [](SectionBuilder &builder) {
	auto ayu = AyuSectionBuilder(builder);
	builder.addSkip();
	BuildService(builder);
	BuildPublic(builder, ayu);
	builder.addSkip();
});

}

rpl::producer<QString> AyuNowPlaying::title() {
	return tr::ayu_NowPlaying();
}

AyuNowPlaying::AyuNowPlaying(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuNowPlaying::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuNowPlayingId() {
	return AyuNowPlaying::Id();
}

}
