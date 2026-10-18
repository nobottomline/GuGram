#include "export/export_video_status.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Export {
namespace {

int Priority(VideoExportState state) {
	switch (state) {
	case VideoExportState::None: return 0;
	case VideoExportState::Complete: return 1;
	case VideoExportState::Cancelled: return 2;
	case VideoExportState::Paused: return 3;
	case VideoExportState::Preparing: return 4;
	case VideoExportState::Scanning: return 5;
	case VideoExportState::Downloading: return 6;
	case VideoExportState::Failed: return 7;
	}
	return 0;
}

} // namespace

VideoExportState CombineVideoExportStates(
		VideoExportState a,
		VideoExportState b) {
	return (Priority(a) >= Priority(b)) ? a : b;
}

QString SaveVideoExportStates(
		const QString &account,
		const VideoExportStates &states) {
	auto peers = QJsonObject();
	for (auto i = states.begin(); i != states.end(); ++i) {
		peers.insert(i.key(), int(i.value()));
	}
	return QString::fromUtf8(QJsonDocument(QJsonObject{
		{ QStringLiteral("schema"), 1 },
		{ QStringLiteral("account"), account },
		{ QStringLiteral("peers"), peers },
	}).toJson(QJsonDocument::Compact));
}

VideoExportStates RestoreVideoExportStates(
		const QString &account,
		const QString &json) {
	const auto root = QJsonDocument::fromJson(json.toUtf8()).object();
	if (root.value(QStringLiteral("schema")).toInt() != 1
		|| root.value(QStringLiteral("account")).toString() != account) {
		return {};
	}
	auto result = VideoExportStates();
	const auto peers = root.value(QStringLiteral("peers")).toObject();
	for (auto i = peers.begin(); i != peers.end(); ++i) {
		auto valid = false;
		const auto peer = i.key().toULongLong(&valid);
		const auto value = i.value().toInt(-1);
		if (!valid || !peer || QString::number(peer) != i.key()
			|| value <= int(VideoExportState::None)
			|| value > int(VideoExportState::Failed)) {
			continue;
		}
		auto state = VideoExportState(value);
		if (state == VideoExportState::Preparing
			|| state == VideoExportState::Scanning
			|| state == VideoExportState::Downloading) {
			state = VideoExportState::Paused;
		}
		result.insert(i.key(), state);
	}
	return result;
}

} // namespace Export
