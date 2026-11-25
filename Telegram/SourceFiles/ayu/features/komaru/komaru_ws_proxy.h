// This is the source code of KomaruGram for Desktop.
#pragma once

#include <rpl/producer.h>

namespace KomaruWsProxy {

// True when the active proxy is the built-in tg-ws-proxy instance.
[[nodiscard]] bool IsActive();
[[nodiscard]] bool IsConnecting();

// Starts the bundled tg-ws-proxy and points Telegram Desktop at it.
void Enable(Fn<void(QString error)> done = nullptr);
void Disable();

// Measures live round-trip latency to Telegram's WebSocket endpoint
// (kws2.web.telegram.org), returning milliseconds or -1 if unreachable.
void Ping(Fn<void(int ms)> done);

[[nodiscard]] rpl::producer<> StateChanged();

} // namespace KomaruWsProxy
