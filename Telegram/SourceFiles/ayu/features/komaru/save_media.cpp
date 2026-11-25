// This is the source code of KomaruGram for Desktop.
#include "ayu/features/komaru/save_media.h"

#include "api/api_toggling_media.h"
#include "apiwrap.h"
#include "ayu/komaru_strings.h"
#include "base/random.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/file_utilities.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "lottie/lottie_frame_generator.h"
#include "main/main_session.h"
#include "media/media_video_encode.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QFile>

namespace KomaruSaveMedia {
namespace {

constexpr auto kPartSize = 512 * 1024;
constexpr auto kBigFileFrom = 10 * 1024 * 1024;
constexpr auto kStickerSide = 512;

[[nodiscard]] QByteArray LoadedBytes(not_null<DocumentData*> document) {
	const auto media = document->createMediaView();
	if (auto bytes = media->bytes(); !bytes.isEmpty()) {
		return bytes;
	}
	auto file = QFile(document->filepath(true));
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Sends the bytes part by part, then calls done with the file to attach.
void UploadParts(
		not_null<Main::Session*> session,
		QByteArray bytes,
		QString name,
		Fn<void(MTPInputFile)> done,
		Fn<void()> fail) {
	const auto id = base::RandomValue<uint64>();
	const auto big = (bytes.size() >= kBigFileFrom);
	const auto count = int((bytes.size() + kPartSize - 1) / kPartSize);
	const auto weak = base::make_weak(session);
	const auto send = std::make_shared<Fn<void(int)>>();
	*send = [=](int index) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		} else if (index == count) {
			done(big
				? MTP_inputFileBig(MTP_long(id), MTP_int(count), MTP_string(name))
				: MTP_inputFile(
					MTP_long(id),
					MTP_int(count),
					MTP_string(name),
					MTP_string()));
			return;
		}
		const auto part = bytes.mid(index * kPartSize, kPartSize);
		const auto next = [=] { (*send)(index + 1); };
		if (big) {
			strong->api().request(MTPupload_SaveBigFilePart(
				MTP_long(id),
				MTP_int(index),
				MTP_int(count),
				MTP_bytes(part)
			)).done(next).fail(fail).send();
		} else {
			strong->api().request(MTPupload_SaveFilePart(
				MTP_long(id),
				MTP_int(index),
				MTP_bytes(part)
			)).done(next).fail(fail).send();
		}
	};
	(*send)(0);
}

void SaveAsGif(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document) {
	const auto session = &document->session();
	auto bytes = LoadedBytes(document);
	if (bytes.isEmpty()) {
		show->showToast(Komaru::Text("save_gif_download_first"));
		return;
	}
	show->showToast(Komaru::Text("save_gif_preparing"));
	const auto weak = base::make_weak(session);
	crl::async([=, bytes = std::move(bytes)]() mutable {
		auto source = Media::Encode::VideoSource();
		source.bytes = std::move(bytes);
		source.removeAudio = true;
		auto job = Media::Encode::Job();
		job.source = std::move(source);
		auto result = Media::Encode::Run(std::move(job));
		crl::on_main([=, result = std::move(result)]() mutable {
			const auto strong = weak.get();
			if (!strong) {
				return;
			} else if (result.empty()) {
				show->showToast(Komaru::Text("save_gif_failed"));
				return;
			}
			const auto failed = [=] {
				show->showToast(Komaru::Text("save_gif_failed"));
			};
			const auto dimensions = result.dimensions;
			const auto duration = result.duration;
			UploadParts(strong, std::move(result.bytes), u"animation.mp4"_q, [=](
					MTPInputFile file) {
				const auto strong = weak.get();
				if (!strong) {
					return;
				}
				using Flag = MTPDinputMediaUploadedDocument::Flag;
				using VideoFlag = MTPDdocumentAttributeVideo::Flag;
				auto attributes = QVector<MTPDocumentAttribute>{
					MTP_documentAttributeVideo(
						MTP_flags(VideoFlag::f_supports_streaming
							| VideoFlag::f_nosound),
						MTP_double(duration / 1000.),
						MTP_int(dimensions.width()),
						MTP_int(dimensions.height()),
						MTPint(),
						MTPdouble(),
						MTPstring()),
					MTP_documentAttributeAnimated(),
					MTP_documentAttributeFilename(
						MTP_string(u"animation.mp4"_q)),
				};
				strong->api().request(MTPmessages_UploadMedia(
					MTP_flags(0),
					MTPstring(),
					MTP_inputPeerSelf(),
					MTP_inputMediaUploadedDocument(
						MTP_flags(Flag::f_nosound_video),
						file,
						MTPInputFile(),
						MTP_string("video/mp4"),
						MTP_vector<MTPDocumentAttribute>(std::move(attributes)),
						MTPVector<MTPInputDocument>(),
						MTPInputPhoto(),
						MTPint(),
						MTPint())
				)).done([=](const MTPMessageMedia &result) {
					const auto strong = weak.get();
					if (!strong) {
						return;
					}
					auto gif = (DocumentData*)nullptr;
					result.match([&](const MTPDmessageMediaDocument &data) {
						if (const auto received = data.vdocument()) {
							gif = strong->data().processDocument(*received);
						}
					}, [](const auto &) {});
					if (!gif) {
						failed();
						return;
					}
					Api::ToggleSavedGif(show, gif, Data::FileOrigin(), true);
				}).fail(failed).send();
			}, failed);
		});
	});
}

// How long one loop of a Lottie animation plays.
[[nodiscard]] crl::time LottieDuration(const QByteArray &bytes) {
	auto generator = Lottie::FrameGenerator(bytes);
	auto result = crl::time(0);
	auto storage = QImage();
	for (auto i = 0; i != 3600; ++i) {
		auto frame = generator.renderNext(std::move(storage), QSize(8, 8));
		if (frame.image.isNull()) {
			break;
		}
		result += std::max(frame.duration, crl::time(1));
		storage = std::move(frame.image);
		if (frame.last) {
			break;
		}
	}
	return result;
}

void SaveStickerAsVideo(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document) {
	auto bytes = LoadedBytes(document);
	if (bytes.isEmpty()) {
		show->showToast(Komaru::Text("save_gif_download_first"));
		return;
	}
	const auto sticker = document->sticker();
	const auto webm = sticker && sticker->isWebm();
	const auto save = [=](QByteArray result, QString extension) {
		FileDialog::GetWritePath(
			nullptr,
			Komaru::Text("save_sticker_video"),
			u"*"_q + extension,
			filedialogDefaultName(u"sticker"_q, extension, QString(), true),
			[=](QString &&path) {
				auto file = QFile(path);
				if (file.open(QIODevice::WriteOnly)) {
					file.write(result);
				}
			});
	};
	if (webm) {
		save(std::move(bytes), u".webm"_q);
		return;
	}
	show->showToast(Komaru::Text("save_gif_preparing"));
	crl::async([=, bytes = std::move(bytes)]() mutable {
		const auto duration = LottieDuration(bytes);
		auto still = Media::Encode::StillSource();
		still.base = QImage(
			QSize(kStickerSide, kStickerSide),
			QImage::Format_ARGB32_Premultiplied);
		still.base.fill(Qt::white);
		still.duration = std::max(duration, crl::time(1000));
		still.fps = 60.;
		auto entity = Media::Encode::AnimatedEntity();
		entity.kind = Media::Encode::AnimatedEntity::Kind::Lottie;
		entity.bytes = std::move(bytes);
		entity.geometry = QRectF(0, 0, kStickerSide, kStickerSide);
		auto job = Media::Encode::Job();
		job.source = std::move(still);
		job.overlay.push_back(std::move(entity));
		auto result = Media::Encode::Run(std::move(job));
		crl::on_main([=, result = std::move(result)]() mutable {
			if (result.empty()) {
				show->showToast(Komaru::Text("save_gif_failed"));
				return;
			}
			save(std::move(result.bytes), u".mp4"_q);
		});
	});
}

} // namespace

void AddActions(
		not_null<Ui::PopupMenu*> menu,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document,
		bool allowed) {
	if (!allowed) {
		return;
	}
	const auto sticker = document->sticker();
	if (document->isVideoFile() && !document->isGifv()) {
		menu->addAction(Komaru::Text("save_as_gif"), [=] {
			SaveAsGif(show, document);
		}, &st::menuIconGif);
	} else if (sticker && (sticker->isLottie() || sticker->isWebm())) {
		menu->addAction(Komaru::Text("save_sticker_video"), [=] {
			SaveStickerAsVideo(show, document);
		}, &st::menuIconDownload);
	}
}

} // namespace KomaruSaveMedia
