#pragma once

#include <QtCore/QMap>
#include <QtCore/QString>

namespace Export {

enum class VideoExportState {
	None = 0,
	Preparing = 1,
	Scanning = 2,
	Downloading = 3,
	Paused = 4,
	Cancelled = 5,
	Complete = 6,
	Failed = 7,
};

using VideoExportStates = QMap<QString, VideoExportState>;

[[nodiscard]] VideoExportState CombineVideoExportStates(
	VideoExportState a,
	VideoExportState b);
[[nodiscard]] QString SaveVideoExportStates(
	const QString &account,
	const VideoExportStates &states);
[[nodiscard]] VideoExportStates RestoreVideoExportStates(
	const QString &account,
	const QString &json);

} // namespace Export
