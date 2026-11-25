// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include <QString>

namespace Window {
class SessionController;
}

namespace Ui {

class GenericBox;

struct PluginMetadata {
	QString id;
	QString name;
	QString description;
	QString author;
	QString version;
	QString icon;
	QString minVersion;
	QStringList requirements;
};

[[nodiscard]] PluginMetadata ParsePluginMetadata(const QByteArray &data);

// Elyx archives (.elyx, .eaf) keep their metadata in a YAML metainfo file.
[[nodiscard]] PluginMetadata ParseElyxMetadata(const QByteArray &bytes);
// .py files count only when they import Telethon, Kurigram or the SDK.
[[nodiscard]] bool IsPluginFileName(const QString &name);
[[nodiscard]] bool IsUserbotSource(const QByteArray &bytes);
[[nodiscard]] PluginMetadata ParsePluginFile(
	const QString &name,
	const QByteArray &bytes);

void ShowPluginInfoBox(
	not_null<Window::SessionController*> controller,
	const QString& pluginPath,
	PluginMetadata metadata);

} // namespace Ui
