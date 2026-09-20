/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings.h"

enum {
	MaxSelectedItems = 100,

	LocalEncryptIterCount = 4000, // key derivation iteration count
	LocalEncryptNoPwdIterCount = 4, // key derivation iteration count without pwd (not secure anyway)
	LocalEncryptSaltSize = 32, // 256 bit

	AutoSearchTimeout = 500, // 0.5 secs

	PreloadHeightsCount = 1, // when 1 screen to scroll left make a preload request

	SearchPeopleLimit = 20,

	WebPageUserId = 701000,

	UpdateDelayConstPart = 8 * 3600, // 8 hour min time between update check requests
	UpdateDelayRandPart = 8 * 3600, // 8 hour max - min time between update check requests

	WrongPasscodeTimeout = 1500,

	ChoosePeerByDragTimeout = 500, // 0.5 secs mouse not moved to choose dialog when dragging a file
};

inline const char *cGUIDStr() {
#ifndef OS_MAC_STORE
	static const char *gGuidStr = "{87A94AB0-E370-4cde-98D3-ACC110C59666}";
#else // OS_MAC_STORE
	static const char *gGuidStr = "{E51FB841-8C0B-4EF9-9E9E-5A0078567666}";
#endif // OS_MAC_STORE

	return gGuidStr;
}

static const char *UpdatesPublicKey = "\
-----BEGIN RSA PUBLIC KEY-----\n\
MIIBCgKCAQEAqm37Z3Z433BbSGgvJNPoEMtM2Q65nom0KmPddahi6Z17RUirQ09k\n\
Jb4xp/2z/Lcq32hUsFBpQ9y8zk6D3oB26urpNuze0FQRECpCQ0nzogy9TKGN2uzE\n\
nnm3ccVfyVnCJCeZ9qakmh/lbNEZUZOqqTGnkQdGatjplZKOWVmOtTqu7JWwzNyO\n\
rg0+YTu5zTNkgzIrw9Ws+o3NcO+dAGFmPRhsqG6eMb2nt1m/2FfNWa1m0xvzRArD\n\
bFrpy4TM1iX33m9bITEYuyNwKXg+tspXBJB2TesYasAame0Kw69XqgA7uNPiX6FQ\n\
sO7tNvIlBJIoGWBln0+vgF17Fph8BP3/vQIDAQAB\n\
-----END RSA PUBLIC KEY-----\
";

static const char *UpdatesPublicBetaKey = "\
-----BEGIN RSA PUBLIC KEY-----\n\
MIIBCgKCAQEA1Bm/hqDwed834TzIC/JN7KOaP+WZtGwWWCIoF1OUMQnWa99UpWKt\n\
xjSeUIaqPbAxJh262pdCOdVcgY0reh+RmahBKQYC2YcAR4tlZPf5kdOCIAGQWJMu\n\
TKhzsIYE+U1aNkTqMv1M9y3NNb0xqWvzN3UiEkxh5C/SXvUWlRvS50GGcZpxLR88\n\
2fSKEWqEKN0+bEfVYBRcwStD002lZcJDaplKmzmrYNA7zWv0FPJ6MWx9Rb+Z/xd5\n\
kzVbVVCRdc/pR6YVm1XnrmrU5eHNrK1/WAdY2UBxotAemqciYpvbu6SLDkA4YxbQ\n\
rlcDDEG4f22eLC8LxCSK0f8nWVQ9dU1sAQIDAQAB\n\
-----END RSA PUBLIC KEY-----\
";

#if defined TDESKTOP_API_ID && defined TDESKTOP_API_HASH

constexpr auto ApiId = TDESKTOP_API_ID;
constexpr auto ApiHash = QT_STRINGIFY(TDESKTOP_API_HASH);

#else // TDESKTOP_API_ID && TDESKTOP_API_HASH

// To build your version of Telegram Desktop you're required to provide
// your own 'api_id' and 'api_hash' for the Telegram API access.
//
// How to obtain your 'api_id' and 'api_hash' is described here:
// https://core.telegram.org/api/obtaining_api_id
//
// If you're building the application not for deployment,
// but only for test purposes you can comment out the error below.
//
// This will allow you to use TEST ONLY 'api_id' and 'api_hash' which are
// very limited by the Telegram API server.
//
// Your users will start getting internal server errors on login
// if you deploy an app using those 'api_id' and 'api_hash'.

#error You are required to provide API_ID and API_HASH.

constexpr auto ApiId = 17349;
constexpr auto ApiHash = "344583e45741c457fe1862106095a5eb";

#endif // TDESKTOP_API_ID && TDESKTOP_API_HASH

#if Q_BYTE_ORDER == Q_BIG_ENDIAN
#error "Only little endian is supported!"
#endif // Q_BYTE_ORDER == Q_BIG_ENDIAN

#if (TDESKTOP_ALPHA_VERSION != 0)

// Private key for downloading closed alphas.
#include "../../../DesktopPrivate/alpha_private.h"

#else
static const char *AlphaPrivateKey = "";
#endif

extern QString gKeyFile;
inline const QString &cDataFile() {
	if (!gKeyFile.isEmpty()) return gKeyFile;
	static const QString res(u"data"_q);
	return res;
}

inline const QRegularExpression &cRussianLetters() {
	static QRegularExpression regexp(QString::fromUtf8("[а-яА-ЯёЁ]"));
	return regexp;
}
