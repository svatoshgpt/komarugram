// This is the source code of KomaruGram for Desktop.
#pragma once

#include "settings/settings_common.h"
#include "settings/settings_common_session.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

class KomaruPluginsSection : public Section<KomaruPluginsSection> {
public:
	KomaruPluginsSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

[[nodiscard]] Type KomaruPluginsId();

// Installs a plugin file, from the plugins screen or a file in a chat, and
// opens its box. Without the runtime it opens the plugins screen instead.
void InstallPluginFile(
	not_null<Window::SessionController*> controller,
	const QString &path,
	Fn<void()> installed = nullptr);

[[nodiscard]] bool PluginInstalled(const QString &id);

} // namespace Settings
