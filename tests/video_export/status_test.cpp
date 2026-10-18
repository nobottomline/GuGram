#include "export/export_video_status.h"
#include "export/export_video_badge.h"
#include "export/export_video_files.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QLockFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTextStream>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <cstdlib>

using namespace Export;
using State = VideoExportState;

namespace {

void Require(bool value, const char *message) {
	if (!value) {
		QTextStream(stderr) << message << '\n';
		std::exit(1);
	}
}

} // namespace

int main(int argc, char **argv) {
	auto app = QCoreApplication(argc, argv);
	for (auto a = 0; a <= int(State::Failed); ++a) {
		for (auto b = 0; b <= int(State::Failed); ++b) {
			Require(CombineVideoExportStates(State(a), State(b))
				== CombineVideoExportStates(State(b), State(a)), "job order must not affect badge");
		}
	}
	Require(CombineVideoExportStates(State::Complete, State::Downloading)
		== State::Downloading, "a completed destination must not hide another running destination");
	Require(CombineVideoExportStates(State::Failed, State::Downloading)
		== State::Failed, "a failure must remain visible while another destination downloads");
	Require(CombineVideoExportStates(State::Complete, State::Paused)
		== State::Paused, "paused work must not show completion");
	Require(CombineVideoExportStates(State::Complete, State::Cancelled)
		== State::Cancelled, "cancelled work must not show completion");

	const auto statesA = VideoExportStates{
		{ "123", State::Downloading },
		{ "18446744073709551615", State::Complete },
		{ "789", State::Failed },
	};
	const auto statesB = VideoExportStates{ { "123", State::Complete } };
	const auto savedA = SaveVideoExportStates("101", statesA);
	const auto savedB = SaveVideoExportStates("202", statesB);
	const auto restoredA = RestoreVideoExportStates("101", savedA);
	Require(restoredA.value("123") == State::Paused, "interrupted work must not claim it is still running");
	Require(restoredA.value("18446744073709551615") == State::Complete, "preserve full-width IDs and completion");
	Require(restoredA.value("789") == State::Failed, "persist attention state");
	Require(RestoreVideoExportStates("202", savedA).isEmpty(), "reject status cache from another account");
	Require(RestoreVideoExportStates("202", savedB).value("123") == State::Complete,
		"identical chat IDs in two accounts must retain independent states");
	Require(RestoreVideoExportStates("101", "not json").isEmpty(), "ignore corrupt status cache");
	Require(RestoreVideoExportStates("101",
		R"({"schema":1,"account":"101","peers":{"123":99,"-1":6,"00012":6}})").isEmpty(),
		"ignore invalid peer IDs and unknown states");

	auto temporary = QTemporaryDir();
	Require(temporary.isValid(), "temporary export root");
	const auto parent = temporary.path();
	const auto folderA = VideoFiles::ChatFolder(parent, "101", "123", "Same chat name");
	const auto folderB = VideoFiles::ChatFolder(parent, "202", "123", "Same chat name");
	Require(folderA != folderB, "two accounts exporting one chat need separate folders");
	Require(QDir().mkpath(folderA) && QDir().mkpath(folderB), "create separate destinations");
	const auto manifestA = VideoFiles::Manifest{ "101", "123", "Same chat name" };
	const auto manifestB = VideoFiles::Manifest{ "202", "123", "Same chat name" };
	Require(VideoFiles::Write(folderA, manifestA).isEmpty(), "write first account receipt");
	Require(VideoFiles::Write(folderB, manifestB).isEmpty(), "write second account receipt");
	auto first = manifestA;
	auto second = manifestB;
	Require(VideoFiles::Read(folderA, first).isEmpty() && VideoFiles::Read(folderB, second).isEmpty(),
		"account receipts must remain independently readable");
	Require(!VideoFiles::Read(folderA, second).isEmpty(), "never reuse another account receipt");
	auto lockA = QLockFile(QDir(folderA).filePath(".video-export.lock"));
	auto lockB = QLockFile(QDir(folderB).filePath(".video-export.lock"));
	Require(lockA.tryLock() && lockB.tryLock(), "two accounts must be able to export the same chat concurrently");
	Require(VideoFiles::ChatFolder(parent, "101", "123", "Renamed chat") == folderA,
		"renaming a chat must not lose its scoped download folder");
	const auto legacy = QDir(parent).filePath("Legacy chat_456");
	Require(QDir().mkpath(legacy), "create legacy layout");
	const auto old = VideoFiles::Manifest{ "101", "456", "Legacy chat" };
	Require(VideoFiles::Write(legacy, old).isEmpty(), "write legacy receipt");
	Require(VideoFiles::ChatFolder(parent, "101", "456", "Legacy chat") == legacy,
		"retain the current account's existing export location");
	Require(VideoFiles::ChatFolder(parent, "101", "456", "Renamed legacy chat") == legacy,
		"renaming a legacy chat must not lose its existing completed files");
	Require(VideoFiles::ChatFolder(parent, "202", "456", "Legacy chat") != legacy,
		"another account must not share a legacy export folder");

	auto preview = QImage(360, 200, QImage::Format_ARGB32_Premultiplied);
	preview.fill(QColor("#ffffff"));
	auto painter = QPainter(&preview);
	const auto states = { State::Downloading, State::Complete, State::Failed, State::Paused, State::Cancelled };
	auto images = QList<QImage>();
	auto row = 0;
	for (const auto size : { 18, 22, 27, 36 }) {
		auto column = 0;
		for (const auto state : states) {
			auto image = QImage(size + 4, size + 4, QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			auto p = QPainter(&image);
			const auto before = p.transform();
			PaintVideoExportBadge(p, QRectF(2, 2, size, size), state,
				QColor("#168acd"), QColor("#ffffff"), size / 9.);
			Require(p.transform() == before, "badge renderer must restore painter state");
			p.end();
			Require(image.pixelColor(0, 0).alpha() == 0, "badge must stay inside its bounds");
			if (row == 0) {
				for (const auto &previous : images) {
					Require(previous != image, "status must remain distinguishable without color");
				}
				images.push_back(image);
			}
			const auto color = state == State::Failed ? QColor("#d84d4d")
				: state == State::Complete ? QColor("#2dad2d") : QColor("#168acd");
			PaintVideoExportBadge(painter, QRectF(12 + column * 70, 8 + row * 48, size, size),
				state, color, QColor("#ffffff"), size / 9.);
			++column;
		}
		++row;
	}
	painter.end();
	if (argc > 1) {
		Require(preview.save(QString::fromLocal8Bit(argv[1])), "save production badge rendering preview");
	}
	QTextStream(stdout) << "Account isolation, folder compatibility, persistent states and badge rendering checks passed.\n";
}
