#include "export/export_video_status.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

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

VideoExportStates ImportVideoExportStates(
		const QString &parent,
		const QString &account,
		const std::atomic<bool> &cancelled) {
	if (parent.isEmpty() || cancelled.load()) {
		return {};
	}
	auto folders = QStringList{ parent };
	const auto appendChildren = [&](const QString &path) {
		if (QFileInfo(path).isSymLink()) {
			return;
		}
		const auto names = QDir(path).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
		for (const auto &name : names) {
			folders.push_back(QDir(path).filePath(name));
		}
	};
	appendChildren(parent);
	appendChildren(QDir(parent).filePath(QStringLiteral("account_") + account));
	auto result = VideoExportStates();
	for (const auto &folder : folders) {
		if (cancelled.load()) {
			break;
		}
		auto file = QFile(QDir(folder).filePath(QStringLiteral("video-export.json")));
		if (QFileInfo(folder).isSymLink() || QFileInfo(file).isSymLink()
			|| !file.open(QIODevice::ReadOnly)) {
			continue;
		}
		const auto root = QJsonDocument::fromJson(file.readAll()).object();
		const auto peer = root.value(QStringLiteral("peer")).toString();
		auto valid = false;
		const auto id = peer.toULongLong(&valid);
		if (!valid || !id || QString::number(id) != peer
			|| root.value(QStringLiteral("schema")).toInt() != 1
			|| !root.value(QStringLiteral("videos")).isArray()
			|| root.value(QStringLiteral("account")).toString() != account) {
			continue;
		}
		const auto key = root.value(QStringLiteral("state")).toString();
		const auto videos = root.value(QStringLiteral("videos")).toArray();
		auto state = VideoExportState::Paused;
		if (key == QStringLiteral("cancelled")) {
			state = VideoExportState::Cancelled;
		} else if (key == QStringLiteral("complete")
			&& root.value(QStringLiteral("scan_complete")).toBool()) {
			state = VideoExportState::Complete;
		}
		auto failed = (key == QStringLiteral("incomplete")
			|| key == QStringLiteral("failed")
			|| root.value(QStringLiteral("unavailable_media")).toInt() > 0);
		for (const auto &video : videos) {
			const auto status = video.toObject().value(QStringLiteral("status")).toString();
			failed |= (status == QStringLiteral("failed")
				|| (state == VideoExportState::Complete && status != QStringLiteral("complete")));
		}
		if (failed) {
			state = VideoExportState::Failed;
		}
		result[peer] = CombineVideoExportStates(result.value(peer, VideoExportState::None), state);
	}
	return result;
}

} // namespace Export
