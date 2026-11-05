// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/data/ayu_database.h"

#include "ayu/data/entities.h"

#include <komaru_core.h>
#include "ayu/libs/sqlite/sqlite3.h"

// The storage itself is Rust code in Telegram/rust/komaru_core.

namespace AyuDatabase {
namespace {

KomaruBytes Bytes(const std::string &value) {
	return { value.data(), value.size() };
}

KomaruBytes Bytes(const std::vector<char> &value) {
	return { value.data(), value.size() };
}

std::string String(KomaruBytes value) {
	return value.size ? std::string(value.data, value.size) : std::string();
}

std::vector<char> Vector(KomaruBytes value) {
	return value.size
		? std::vector<char>(value.data, value.data + value.size)
		: std::vector<char>();
}

KomaruMessage ToRow(const AyuMessageBase &m) {
	return {
		.fakeId = m.fakeId,
		.userId = m.userId,
		.dialogId = m.dialogId,
		.groupedId = m.groupedId,
		.peerId = m.peerId,
		.fromId = m.fromId,
		.topicId = m.topicId,
		.messageId = m.messageId,
		.date = m.date,
		.flags = m.flags,
		.editDate = m.editDate,
		.views = m.views,
		.fwdFlags = m.fwdFlags,
		.fwdFromId = m.fwdFromId,
		.fwdName = Bytes(m.fwdName),
		.fwdDate = m.fwdDate,
		.fwdPostAuthor = Bytes(m.fwdPostAuthor),
		.replyFlags = m.replyFlags,
		.replyMessageId = m.replyMessageId,
		.replyPeerId = m.replyPeerId,
		.replyTopId = m.replyTopId,
		.replyForumTopic = uint8_t(m.replyForumTopic ? 1 : 0),
		.replySerialized = Bytes(m.replySerialized),
		.entityCreateDate = m.entityCreateDate,
		.text = Bytes(m.text),
		.textEntities = Bytes(m.textEntities),
		.mediaPath = Bytes(m.mediaPath),
		.hqThumbPath = Bytes(m.hqThumbPath),
		.documentType = m.documentType,
		.documentSerialized = Bytes(m.documentSerialized),
		.thumbsSerialized = Bytes(m.thumbsSerialized),
		.documentAttributesSerialized = Bytes(m.documentAttributesSerialized),
		.mimeType = Bytes(m.mimeType),
	};
}

template <typename Message>
void FromRow(const KomaruMessage &row, Message &m) {
	m.fakeId = row.fakeId;
	m.userId = row.userId;
	m.dialogId = row.dialogId;
	m.groupedId = row.groupedId;
	m.peerId = row.peerId;
	m.fromId = row.fromId;
	m.topicId = row.topicId;
	m.messageId = row.messageId;
	m.date = row.date;
	m.flags = row.flags;
	m.editDate = row.editDate;
	m.views = row.views;
	m.fwdFlags = row.fwdFlags;
	m.fwdFromId = row.fwdFromId;
	m.fwdName = String(row.fwdName);
	m.fwdDate = row.fwdDate;
	m.fwdPostAuthor = String(row.fwdPostAuthor);
	m.replyFlags = row.replyFlags;
	m.replyMessageId = row.replyMessageId;
	m.replyPeerId = row.replyPeerId;
	m.replyTopId = row.replyTopId;
	m.replyForumTopic = (row.replyForumTopic != 0);
	m.replySerialized = Vector(row.replySerialized);
	m.entityCreateDate = row.entityCreateDate;
	m.text = String(row.text);
	m.textEntities = Vector(row.textEntities);
	m.mediaPath = String(row.mediaPath);
	m.hqThumbPath = String(row.hqThumbPath);
	m.documentType = row.documentType;
	m.documentSerialized = Vector(row.documentSerialized);
	m.thumbsSerialized = Vector(row.thumbsSerialized);
	m.documentAttributesSerialized = Vector(row.documentAttributesSerialized);
	m.mimeType = String(row.mimeType);
}

template <typename Message>
void CollectMessage(void *context, const KomaruMessage *row) {
	auto &result = *static_cast<std::vector<Message>*>(context);
	FromRow(*row, result.emplace_back());
}

KomaruRegexFilter ToRow(const RegexFilter &filter) {
	return {
		.id = Bytes(filter.id),
		.text = Bytes(filter.text),
		.enabled = uint8_t(filter.enabled ? 1 : 0),
		.reversed = uint8_t(filter.reversed ? 1 : 0),
		.caseInsensitive = uint8_t(filter.caseInsensitive ? 1 : 0),
		.hasDialogId = uint8_t(filter.dialogId.has_value() ? 1 : 0),
		.dialogId = filter.dialogId.value_or(0),
	};
}

void CollectFilter(void *context, const KomaruRegexFilter *row) {
	auto &result = *static_cast<std::vector<RegexFilter>*>(context);
	auto &filter = result.emplace_back();
	filter.id = Vector(row->id);
	filter.text = String(row->text);
	filter.enabled = (row->enabled != 0);
	filter.reversed = (row->reversed != 0);
	filter.caseInsensitive = (row->caseInsensitive != 0);
	if (row->hasDialogId) {
		filter.dialogId = row->dialogId;
	}
}

void CollectExclusion(void *context, const KomaruFilterExclusion *row) {
	auto &result = *static_cast<std::vector<RegexFilterGlobalExclusion>*>(
		context);
	result.push_back({
		.fakeId = row->fakeId,
		.dialogId = row->dialogId,
		.filterId = Vector(row->filterId),
	});
}

std::vector<RegexFilter> Filters(int32_t which, ID dialogId = 0) {
	auto result = std::vector<RegexFilter>();
	komaru_db_filters(which, dialogId, CollectFilter, &result);
	return result;
}

void Log(const char *text, size_t size) {
	LOG(("%1").arg(QString::fromUtf8(text, qsizetype(size))));
}

} // namespace

void initialize() {
	static const auto sqlite = KomaruSqlite{
		.initialize = sqlite3_initialize,
		.open_v2 = sqlite3_open_v2,
		.close_v2 = sqlite3_close_v2,
		.errmsg = sqlite3_errmsg,
		.exec = sqlite3_exec,
		.prepare_v2 = sqlite3_prepare_v2,
		.finalize = sqlite3_finalize,
		.step = sqlite3_step,
		.bind_int64 = sqlite3_bind_int64,
		.bind_null = sqlite3_bind_null,
		.bind_text = sqlite3_bind_text,
		.bind_blob = sqlite3_bind_blob,
		.column_type = sqlite3_column_type,
		.column_int64 = sqlite3_column_int64,
		.column_blob = sqlite3_column_blob,
		.column_bytes = sqlite3_column_bytes,
		.freeMemory = sqlite3_free,
	};
	komaru_set_sqlite(&sqlite);
	komaru_set_logger(Log);
	const auto path = std::string("./tdata/ayudata.db");
	komaru_db_initialize(path.data(), path.size());
}

void addEditedMessage(const EditedMessage &message) {
	const auto row = ToRow(message);
	komaru_db_add_message(1, &row);
}

std::vector<EditedMessage> getEditedMessages(
		ID userId,
		ID dialogId,
		ID messageId,
		ID minId,
		ID maxId,
		int totalLimit) {
	auto result = std::vector<EditedMessage>();
	komaru_db_edited_messages(
		userId,
		dialogId,
		messageId,
		minId,
		maxId,
		totalLimit,
		CollectMessage<EditedMessage>,
		&result);
	return result;
}

bool hasRevisions(ID userId, ID dialogId, ID messageId) {
	return komaru_db_has_revisions(userId, dialogId, messageId) != 0;
}

void addDeletedMessage(const DeletedMessage &message) {
	const auto row = ToRow(message);
	komaru_db_add_message(0, &row);
}

std::vector<DeletedMessage> getDeletedMessages(
		ID userId,
		ID dialogId,
		ID topicId,
		ID minId,
		ID maxId,
		int totalLimit,
		const std::string &searchQuery) {
	auto result = std::vector<DeletedMessage>();
	komaru_db_deleted_messages(
		userId,
		dialogId,
		topicId,
		minId,
		maxId,
		totalLimit,
		searchQuery.data(),
		searchQuery.size(),
		CollectMessage<DeletedMessage>,
		&result);
	return result;
}

bool hasDeletedMessages(ID userId, ID dialogId, ID topicId) {
	return komaru_db_has_deleted_messages(userId, dialogId, topicId) != 0;
}

void removeDeletedMessage(ID userId, ID dialogId, ID messageId) {
	komaru_db_remove_deleted_message(userId, dialogId, messageId);
}

void clearDeletedMessages(ID userId, ID dialogId, ID topicId) {
	komaru_db_clear_deleted_messages(userId, dialogId, topicId);
}

std::vector<RegexFilter> getAllRegexFilters() {
	return Filters(KomaruFiltersAll);
}

std::vector<RegexFilter> getShared() {
	return Filters(KomaruFiltersShared);
}

std::vector<RegexFilter> getByDialogId(ID dialogId) {
	return Filters(KomaruFiltersForDialog, dialogId);
}

std::vector<RegexFilter> getExcludedByDialogId(ID dialogId) {
	return Filters(KomaruFiltersExcludedForDialog, dialogId);
}

std::vector<RegexFilterGlobalExclusion> getAllFiltersExclusions() {
	auto result = std::vector<RegexFilterGlobalExclusion>();
	komaru_db_exclusions(CollectExclusion, &result);
	return result;
}

void addRegexFilter(const RegexFilter &filter) {
	const auto row = ToRow(filter);
	komaru_db_put_filter(&row);
}

void addRegexExclusion(const RegexFilterGlobalExclusion &exclusion) {
	komaru_db_add_exclusion(
		exclusion.dialogId,
		exclusion.filterId.data(),
		exclusion.filterId.size());
}

void updateRegexFilter(const RegexFilter &filter) {
	const auto row = ToRow(filter);
	komaru_db_update_filter(&row);
}

void deleteFilter(const std::vector<char> &id) {
	komaru_db_delete_filter(id.data(), id.size());
}

void deleteExclusionsByFilterId(const std::vector<char> &id) {
	komaru_db_delete_exclusions_of_filter(id.data(), id.size());
}

void deleteExclusion(ID dialogId, std::vector<char> filterId) {
	komaru_db_delete_exclusion(dialogId, filterId.data(), filterId.size());
}

void deleteAllFilters() {
	komaru_db_delete_all_filters();
}

void deleteAllExclusions() {
	komaru_db_delete_all_exclusions();
}

bool hasFilters() {
	return komaru_db_has_filters() != 0;
}

bool hasPerDialogFilters() {
	return komaru_db_has_per_dialog_filters() != 0;
}

} // namespace AyuDatabase
