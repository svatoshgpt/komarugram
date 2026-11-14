// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/data/messages_storage.h"

#include "ayu/ayu_settings.h"
#include "ayu/data/ayu_database.h"
#include "ayu/utils/ayu_mapper.h"
#include "ayu/utils/telegram_helpers.h"
#include "base/unixtime.h"
#include "data/data_forum_topic.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_session.h"

namespace AyuMessages {

template<typename DerivedMessage>
std::vector<AyuMessageBase> convertToBase(const std::vector<DerivedMessage> &messages) {
	std::vector<AyuMessageBase> based;
	based.reserve(messages.size());
	for (const auto &msg : messages) {
		based.push_back(static_cast<AyuMessageBase>(msg));
	}
	return based;
}

void map(not_null<HistoryItem*> item, AyuMessageBase &message) {
	const ID userId = item->history()->owner().session().userId().bare & PeerId::kChatTypeMask;

	message.userId = userId;
	message.dialogId = getDialogIdFromPeer(item->history()->peer);
	message.groupedId = item->groupId().raw();
	message.peerId = item->history()->peer->id.value & PeerId::kChatTypeMask;
	message.fromId = item->from()->id.value & PeerId::kChatTypeMask;
	if (item->topic()) {
		message.topicId = item->topicRootId().bare;
	} else {
		message.topicId = 0;
	}
	message.messageId = item->id.bare;
	message.date = item->date();
	message.flags = AyuMapper::mapItemFlagsToMTPFlags(item);

	if (const auto edited = item->Get<HistoryMessageEdited>()) {
		message.editDate = edited->date;
	} else {
		message.editDate = base::unixtime::now();
	}

	message.views = item->viewsCount();
	message.fwdFlags = 0;
	message.fwdFromId = 0;
	// message.fwdName
	message.fwdDate = 0;
	// message.fwdPostAuthor
	if (const auto msgsigned = item->Get<HistoryMessageSigned>()) {
		message.postAuthor = msgsigned->author.toStdString();
	}
	message.replyFlags = 0;
	message.replyMessageId = 0;
	message.replyPeerId = 0;
	message.replyTopId = 0;
	message.replyForumTopic = false;
	// message.replySerialized
	// message.replyMarkupSerialized
	message.entityCreateDate = base::unixtime::now();

	auto serializedText = AyuMapper::serializeTextWithEntities(item);
	message.text = serializedText.first;
	message.textEntities = serializedText.second;

	// todo: implement mapping
	message.mediaPath = "/";
	// message.hqThumbPath
	message.documentType = 0; // document type none
	// message.documentSerialized
	// message.thumbsSerialized
	// message.documentAttributesSerialized
	// message.mimeType
}

void addEditedMessage(not_null<HistoryItem *> item) {
	EditedMessage message;
	map(item, message);

	if (message.text.empty()) {
		return;
	}

	AyuDatabase::addEditedMessage(message);
}

std::vector<AyuMessageBase> getEditedMessages(not_null<HistoryItem*> item, ID minId, ID maxId, int totalLimit) {
	const ID userId = item->history()->owner().session().userId().bare & PeerId::kChatTypeMask;
	const auto dialogId = getDialogIdFromPeer(item->history()->peer);
	const auto msgId = item->id.bare;

	return convertToBase(AyuDatabase::getEditedMessages(userId, dialogId, msgId, minId, maxId, totalLimit));
}

bool hasRevisions(not_null<HistoryItem*> item) {
	const ID userId = item->history()->owner().session().userId().bare & PeerId::kChatTypeMask;
	const auto dialogId = getDialogIdFromPeer(item->history()->peer);
	const auto msgId = item->id.bare;

	return AyuDatabase::hasRevisions(userId, dialogId, msgId);
}

void addDeletedMessage(not_null<HistoryItem*> item) {
	DeletedMessage message;
	map(item, message);

	if (message.text.empty()) {
		return;
	}

	AyuDatabase::addDeletedMessage(message);
}

std::vector<AyuMessageBase>
getDeletedMessages(not_null<PeerData*> peer, ID topicId, ID minId, ID maxId, int totalLimit, const QString &searchQuery) {
	const ID userId = peer->session().userId().bare & PeerId::kChatTypeMask;
	return convertToBase(
		AyuDatabase::getDeletedMessages(userId, getDialogIdFromPeer(peer), topicId, minId, maxId, totalLimit, searchQuery.toStdString()));
}

bool hasDeletedMessages(not_null<PeerData*> peer, ID topicId) {
	const ID userId = peer->session().userId().bare & PeerId::kChatTypeMask;
	return AyuDatabase::hasDeletedMessages(userId, getDialogIdFromPeer(peer), topicId);
}

void removeDeletedMessage(not_null<HistoryItem*> item) {
	const auto peer = item->history()->peer;
	const ID userId = peer->session().userId().bare & PeerId::kChatTypeMask;
	AyuDatabase::removeDeletedMessage(userId, getDialogIdFromPeer(peer), item->id.bare);
}

QVector<MTPMessage> withDeletedMessages(
		not_null<History*> history,
		const QVector<MTPMessage> &slice,
		MsgId after,
		MsgId before,
		base::flat_set<MsgId> &restored) {
	constexpr auto kPerPage = 300;
	constexpr auto kSavedOut = 0x00000002;

	if (!AyuSettings::getInstance().saveDeletedMessages()) {
		return slice;
	}
	const auto peer = history->peer;
	const auto owner = &history->owner();
	const auto saved = getDeletedMessages(
		peer,
		0,
		after.bare,
		before.bare,
		kPerPage);
	if (saved.empty()) {
		return slice;
	}
	auto taken = base::flat_set<MsgId>();
	for (const auto &message : slice) {
		taken.emplace(IdFromMessage(message));
	}
	auto result = slice;
	for (const auto &message : saved) {
		const auto id = MsgId(message.messageId);
		if (message.messageId <= 0
			|| !taken.emplace(id).second
			|| owner->message(peer->id, id)) {
			continue;
		}
		using Flag = MTPDmessage::Flag;
		const auto out = (message.flags & kSavedOut) != 0;
		const auto post = peer->isBroadcast();
		auto from = PeerId();
		if (post) {
		} else if (out) {
			from = peer->session().userPeerId();
		} else if (peer->isUser()) {
			from = peer->id;
		} else if (const auto channel = owner->channelLoaded(message.fromId)) {
			from = channel->id;
		} else {
			from = peerFromUser(UserId(BareId(message.fromId)));
		}
		const auto entities = message.textEntities.empty()
			? MTPVector<MTPMessageEntity>()
			: AyuMapper::deserializeTextWithEntities(message.textEntities);
		const auto flags = Flag()
			| (out ? Flag::f_out : Flag())
			| (post ? Flag::f_post : Flag())
			| (from ? Flag::f_from_id : Flag())
			| (entities.v.isEmpty() ? Flag() : Flag::f_entities)
			| (message.postAuthor.empty() ? Flag() : Flag::f_post_author);
		result.push_back(MTP_message(
			MTP_flags(flags),
			MTP_int(message.messageId),
			from ? peerToMTP(from) : MTPPeer(),
			MTPint(), // from_boosts_applied
			MTPstring(), // from_rank
			peerToMTP(peer->id),
			MTPPeer(), // saved_peer_id
			MTPMessageFwdHeader(),
			MTPlong(), // via_bot_id
			MTPlong(), // via_business_bot_id
			MTPPeer(), // guestchat_via_from
			MTPMessageReplyHeader(),
			MTP_int(message.date),
			MTP_string(QString::fromStdString(message.text)),
			MTPMessageMedia(),
			MTPReplyMarkup(),
			entities,
			MTPint(), // views
			MTPint(), // forwards
			MTPMessageReplies(),
			MTPint(), // edit_date
			MTP_string(QString::fromStdString(message.postAuthor)),
			MTPlong(), // grouped_id
			MTPMessageReactions(),
			MTPVector<MTPRestrictionReason>(),
			MTPint(), // ttl_period
			MTPint(), // quick_reply_shortcut_id
			MTPlong(), // effect
			MTPFactCheck(),
			MTPint(), // report_delivery_until_date
			MTPlong(), // paid_message_stars
			MTPSuggestedPost(),
			MTPint(), // schedule_repeat_period
			MTPstring(), // summary_from_language
			MTPRichMessage()));
		restored.emplace(id);
	}
	if (restored.empty()) {
		return slice;
	}
	// Server pages come newest first, the restored ones take their places.
	std::stable_sort(result.begin(), result.end(), [](
			const MTPMessage &a,
			const MTPMessage &b) {
		return IdFromMessage(a) > IdFromMessage(b);
	});
	return result;
}

void clearDeletedMessages(not_null<PeerData*> peer, ID topicId) {
	const ID userId = peer->session().userId().bare & PeerId::kChatTypeMask;
	AyuDatabase::clearDeletedMessages(userId, getDialogIdFromPeer(peer), topicId);
}

}
