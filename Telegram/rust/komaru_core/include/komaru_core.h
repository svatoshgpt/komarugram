// This is the source code of KomaruGram for Desktop.
//
// The C API of the Rust crate in Telegram/rust/komaru_core. Keep the
// structs in sync with src/db.rs and the functions with src/ffi.rs.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KomaruBytes {
	const char *data;
	size_t size;
} KomaruBytes;

typedef struct KomaruMessage {
	int64_t fakeId;
	int64_t userId;
	int64_t dialogId;
	int64_t groupedId;
	int64_t peerId;
	int64_t fromId;
	int64_t topicId;
	int32_t messageId;
	int32_t date;
	int32_t flags;
	int32_t editDate;
	int32_t views;
	int32_t fwdFlags;
	int64_t fwdFromId;
	KomaruBytes fwdName;
	int32_t fwdDate;
	KomaruBytes fwdPostAuthor;
	int32_t replyFlags;
	int32_t replyMessageId;
	int64_t replyPeerId;
	int32_t replyTopId;
	uint8_t replyForumTopic;
	KomaruBytes replySerialized;
	int32_t entityCreateDate;
	KomaruBytes text;
	KomaruBytes textEntities;
	KomaruBytes mediaPath;
	KomaruBytes hqThumbPath;
	int32_t documentType;
	KomaruBytes documentSerialized;
	KomaruBytes thumbsSerialized;
	KomaruBytes documentAttributesSerialized;
	KomaruBytes mimeType;
} KomaruMessage;

typedef struct KomaruRegexFilter {
	KomaruBytes id;
	KomaruBytes text;
	uint8_t enabled;
	uint8_t reversed;
	uint8_t caseInsensitive;
	uint8_t hasDialogId;
	int64_t dialogId;
} KomaruRegexFilter;

typedef struct KomaruFilterExclusion {
	int64_t fakeId;
	int64_t dialogId;
	KomaruBytes filterId;
} KomaruFilterExclusion;

// Rows passed to a sink are valid only during the call.
typedef void (*KomaruMessageSink)(void *context, const KomaruMessage *row);
typedef void (*KomaruFilterSink)(void *context, const KomaruRegexFilter *row);
typedef void (*KomaruExclusionSink)(
	void *context,
	const KomaruFilterExclusion *row);

typedef void (*KomaruLogger)(const char *text, size_t size);

enum {
	KomaruFiltersAll = 0,
	KomaruFiltersShared = 1,
	KomaruFiltersForDialog = 2,
	KomaruFiltersExcludedForDialog = 3,
};

void komaru_set_logger(KomaruLogger logger);

void komaru_db_initialize(const char *path, size_t size);

void komaru_db_add_message(uint8_t edited, const KomaruMessage *message);
void komaru_db_edited_messages(
	int64_t userId,
	int64_t dialogId,
	int64_t messageId,
	int64_t minId,
	int64_t maxId,
	int64_t limit,
	KomaruMessageSink sink,
	void *context);
uint8_t komaru_db_has_revisions(
	int64_t userId,
	int64_t dialogId,
	int64_t messageId);

void komaru_db_deleted_messages(
	int64_t userId,
	int64_t dialogId,
	int64_t topicId,
	int64_t minId,
	int64_t maxId,
	int64_t limit,
	const char *query,
	size_t querySize,
	KomaruMessageSink sink,
	void *context);
uint8_t komaru_db_has_deleted_messages(
	int64_t userId,
	int64_t dialogId,
	int64_t topicId);
void komaru_db_remove_deleted_message(
	int64_t userId,
	int64_t dialogId,
	int64_t messageId);
void komaru_db_clear_deleted_messages(
	int64_t userId,
	int64_t dialogId,
	int64_t topicId);

void komaru_db_filters(
	int32_t which,
	int64_t dialogId,
	KomaruFilterSink sink,
	void *context);
void komaru_db_exclusions(KomaruExclusionSink sink, void *context);
void komaru_db_put_filter(const KomaruRegexFilter *filter);
void komaru_db_update_filter(const KomaruRegexFilter *filter);
void komaru_db_delete_filter(const char *id, size_t size);
void komaru_db_add_exclusion(
	int64_t dialogId,
	const char *filterId,
	size_t size);
void komaru_db_delete_exclusions_of_filter(const char *filterId, size_t size);
void komaru_db_delete_exclusion(
	int64_t dialogId,
	const char *filterId,
	size_t size);
void komaru_db_delete_all_filters();
void komaru_db_delete_all_exclusions();
uint8_t komaru_db_has_filters();
uint8_t komaru_db_has_per_dialog_filters();

#ifdef __cplusplus
} // extern "C"
#endif
