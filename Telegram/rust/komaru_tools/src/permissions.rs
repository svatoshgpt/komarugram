//! `komaru tl-permissions --scheme api.tl --out table.cpp`: which plugin
//! permission each API method needs, from the method names in the scheme.

use std::fmt::Write as _;

use crate::args::Args;

/// The C++ `PluginAction` names, in the order of the enum.
#[cfg(test)]
const ACTIONS: &[&str] = &[
    "Base", "Forbidden", "Other",
    "MessagesRead", "MessagesSend", "MessagesMarkRead", "MessagesEdit", "MessagesDelete",
    "ProfileInfo", "ProfilePhoto", "ProfileUsername", "ProfileStatus", "ProfileMusic",
    "ChatsJoin", "ChatsManage", "Contacts",
    "GiftsView", "GiftsBuy", "GiftsTransfer",
    "StoriesView", "StoriesPost",
];

const FORBIDDEN: &[&str] = &[
    "account.getAuthorizations", "account.resetAuthorization",
    "account.getWebAuthorizations", "account.resetWebAuthorization",
    "account.resetWebAuthorizations", "account.changeAuthorizationSettings",
    "account.updatePasswordSettings", "account.getPasswordSettings",
    "account.getTmpPassword", "account.resetPassword", "account.declinePasswordReset",
    "account.confirmPasswordEmail", "account.resendPasswordEmail",
    "account.cancelPasswordEmail", "account.deleteAccount", "account.setAccountTTL",
    "account.sendChangePhoneCode", "account.changePhone",
    "account.sendConfirmPhoneCode", "account.confirmPhone",
    "account.sendVerifyEmailCode", "account.verifyEmail",
    "account.invalidateSignInCodes", "account.initTakeoutSession",
    "account.finishTakeoutSession", "account.getAuthorizationForm",
    "account.acceptAuthorization", "account.getAllSecureValues",
    "account.getSecureValue", "account.saveSecureValue", "account.deleteSecureValue",
    "initConnection", "invokeWithTakeout", "invokeWithBusinessConnection",
    "invokeWithMessagesRange", "invokeWithApnsSecret", "invokeWithReCaptcha",
    "invokeWithGooglePlayIntegrity",
];

const BASE: &[&str] = &[
    "users.getUsers", "users.getFullUser", "contacts.resolveUsername",
    "messages.getStickerSet", "messages.getAvailableReactions",
    "messages.getCustomEmojiDocuments", "messages.checkChatInvite",
    "upload.getFile", "upload.saveFilePart", "upload.saveBigFilePart",
    "upload.getCdnFile", "upload.getCdnFileHashes", "upload.getFileHashes",
    "photos.getUserPhotos", "invokeWithLayer", "invokeWithoutUpdates",
    "invokeAfterMsg", "invokeAfterMsgs",
];

fn starts(name: &str, prefixes: &[&str]) -> bool {
    prefixes.iter().any(|p| name.starts_with(p))
}

/// The permission a method needs, by its full TL name.
pub fn action(name: &str) -> &'static str {
    if name.starts_with("auth.") || FORBIDDEN.contains(&name) {
        return "Forbidden";
    }
    if BASE.contains(&name) || starts(name, &["help.", "updates.", "langpack."]) {
        return "Base";
    }
    let (space, method) = name.split_once('.').unwrap_or(("", name));
    match space {
        "messages" | "channels" => {
            if starts(method, &["delete"]) && !matches!(method, "deleteChat" | "deleteChannel" | "deleteChatUser" | "deleteExportedChatInvite" | "deleteRevokedExportedChatInvites" | "deleteTopicHistory") {
                return "MessagesDelete";
            }
            if starts(method, &["read", "markDialogUnread"]) {
                return "MessagesMarkRead";
            }
            if starts(method, &["edit"]) && method.contains("Message") {
                return "MessagesEdit";
            }
            if starts(method, &["send", "forward", "setTyping", "uploadMedia", "sendReaction"]) {
                return "MessagesSend";
            }
            if matches!(method, "joinChannel" | "leaveChannel" | "importChatInvite") {
                return "ChatsJoin";
            }
            if starts(method, &["get", "search"]) {
                return "MessagesRead";
            }
            if starts(method, &[
                "edit", "create", "addChatUser", "deleteChatUser", "deleteChat",
                "deleteChannel", "inviteToChannel", "toggle", "set", "update",
                "migrateChat", "exportChatInvite", "deleteExportedChatInvite",
                "deleteRevokedExportedChatInvites", "hideChatJoinRequest",
                "hideAllChatJoinRequests", "convertToGigagroup", "reorder",
                "deleteTopicHistory", "togglePreHistoryHidden",
            ]) {
                return "ChatsManage";
            }
            "Other"
        }
        "account" => match method {
            "updateProfile" | "updateBirthday" | "updatePersonalChannel" => "ProfileInfo",
            "updateUsername" | "checkUsername" | "reorderUsernames" | "toggleUsername" => "ProfileUsername",
            "updateStatus" | "updateEmojiStatus" | "updateColor" => "ProfileStatus",
            m if m.contains("Music") => "ProfileMusic",
            _ => "Other",
        },
        "photos" => "ProfilePhoto",
        "contacts" => "Contacts",
        "payments" => {
            if starts(method, &["sendStarsForm", "sendPaymentForm"]) {
                "GiftsBuy"
            } else if starts(method, &[
                "transferStarGift", "convertStarGift", "saveStarGift",
                "upgradeStarGift", "updateStarGiftPrice", "toggleStarGiftsPinnedToTop",
            ]) {
                "GiftsTransfer"
            } else if starts(method, &["get", "check", "validate"]) {
                "GiftsView"
            } else {
                "Other"
            }
        }
        "stories" => {
            if starts(method, &["get", "read", "incrementStoryViews", "search"]) {
                "StoriesView"
            } else {
                "StoriesPost"
            }
        }
        _ => "Other",
    }
}

/// `name#hex` for every function in the scheme.
pub fn methods(scheme: &str) -> Vec<(String, u32)> {
    let mut functions = false;
    let mut result = vec![];
    for line in scheme.lines() {
        let line = line.trim();
        if line == "---functions---" {
            functions = true;
            continue;
        } else if line == "---types---" {
            functions = false;
            continue;
        }
        if !functions || line.is_empty() || line.starts_with("//") {
            continue;
        }
        let head = line.split_whitespace().next().unwrap_or("");
        let Some((name, id)) = head.split_once('#') else { continue };
        if let Ok(id) = u32::from_str_radix(id, 16) {
            result.push((name.to_string(), id));
        }
    }
    result
}

pub fn render(list: &[(String, u32)]) -> String {
    let mut out = String::from(
        "// Generated by `komaru tl-permissions` from mtproto/scheme/api.tl; do not edit.\n\
         #include \"ayu/features/plugins/plugin_permissions.h\"\n\n\
         namespace KomaruPlugins {\n\n\
         MethodInfo LookupMethod(uint32 id) {\n\
         \tswitch (id) {\n",
    );
    for (name, id) in list {
        let _ = writeln!(
            out,
            "\tcase 0x{id:08x}U: return {{ \"{name}\", PluginAction::{} }};",
            action(name)
        );
    }
    out.push_str("\t}\n\treturn { nullptr, PluginAction::Other };\n}\n\n} // namespace KomaruPlugins\n");
    out
}

pub fn run(list: &[String]) -> Result<u8, String> {
    let args = Args::parse(list)?;
    let scheme = std::fs::read_to_string(args.required("scheme")?)
        .map_err(|e| format!("cannot read the scheme: {e}"))?;
    let out = args.required("out")?;
    let list = methods(&scheme);
    if list.is_empty() {
        return Err("no functions in the scheme".into());
    }
    let text = render(&list).replace('\n', "\r\n");
    std::fs::write(out, text).map_err(|e| format!("cannot write {out}: {e}"))?;
    println!("{} methods", list.len());
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn forbidden_never_lands_in_a_group() {
        for name in FORBIDDEN {
            assert_eq!(action(name), "Forbidden", "{name}");
        }
        assert_eq!(action("auth.exportAuthorization"), "Forbidden");
    }

    #[test]
    fn groups() {
        assert_eq!(action("messages.sendMessage"), "MessagesSend");
        assert_eq!(action("messages.sendScreenshotNotification"), "MessagesSend");
        assert_eq!(action("messages.getHistory"), "MessagesRead");
        assert_eq!(action("messages.readHistory"), "MessagesMarkRead");
        assert_eq!(action("channels.readHistory"), "MessagesMarkRead");
        assert_eq!(action("messages.editMessage"), "MessagesEdit");
        assert_eq!(action("messages.deleteMessages"), "MessagesDelete");
        assert_eq!(action("channels.deleteMessages"), "MessagesDelete");
        assert_eq!(action("messages.deleteHistory"), "MessagesDelete");
        assert_eq!(action("messages.deleteChatUser"), "ChatsManage");
        assert_eq!(action("channels.editTitle"), "ChatsManage");
        assert_eq!(action("channels.joinChannel"), "ChatsJoin");
        assert_eq!(action("account.updateProfile"), "ProfileInfo");
        assert_eq!(action("account.saveMusic"), "ProfileMusic");
        assert_eq!(action("photos.uploadProfilePhoto"), "ProfilePhoto");
        assert_eq!(action("payments.transferStarGift"), "GiftsTransfer");
        assert_eq!(action("payments.sendStarsForm"), "GiftsBuy");
        assert_eq!(action("payments.getStarsStatus"), "GiftsView");
        assert_eq!(action("stories.sendStory"), "StoriesPost");
        assert_eq!(action("stories.getAllStories"), "StoriesView");
        assert_eq!(action("users.getUsers"), "Base");
        assert_eq!(action("updates.getState"), "Base");
        assert_eq!(action("bots.setBotInfo"), "Other");
    }

    #[test]
    fn every_action_is_known() {
        let scheme = include_str!("../../../SourceFiles/mtproto/scheme/api.tl");
        let list = methods(scheme);
        assert!(list.len() > 500);
        for (name, _) in &list {
            assert!(ACTIONS.contains(&action(name)), "{name}");
        }
        assert!(list.iter().any(|(n, id)| n == "messages.sendMessage" && *id == 0xfef48f62));
    }
}
