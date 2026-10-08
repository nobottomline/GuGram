#include "export/export_video_files.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTextStream>
#include <cstdlib>

using namespace Export::VideoFiles;

namespace {

void Require(bool value, const char *message) {
	if (!value) {
		QTextStream(stderr) << message << '\n';
		std::exit(1);
	}
}

void Put(const QString &path, const QByteArray &bytes) {
	auto file = QFile(path);
	Require(file.open(QIODevice::WriteOnly), "open fixture");
	Require(file.write(bytes) == bytes.size(), "write fixture");
}

QByteArray Get(const QString &path) {
	auto file = QFile(path);
	Require(file.open(QIODevice::ReadOnly), "read fixture");
	return file.readAll();
}

} // namespace

int main(int argc, char **argv) {
	auto app = QCoreApplication(argc, argv);
	auto temporary = QTemporaryDir();
	Require(temporary.isValid(), "temporary folder");
	const auto folder = temporary.path();
	const auto path = [&](const QString &name) { return QDir(folder).filePath(name); };
	auto entry = Entry{
		"123", "456", "18446744073709551615", "", 3, "complete",
	};
	entry.name = FileName(entry, "../../unsafe\\name:.mp4");
	Require(!entry.name.contains('/') && !entry.name.contains('\\'), "sanitize remote names");
	Require(!entry.name.contains(':'), "portable remote names");
	const auto longName = FileName(entry, QString(150, QChar(0x4e00)) + ".mkv");
	Require(longName.toUtf8().size() < 210, "leave space for atomic temporary filenames");
	Require(longName.endsWith(".mkv"), "preserve container extension after truncation");
	auto repeated = entry;
	repeated.message = "457";
	Require(FileName(repeated, "../../unsafe\\name:.mp4") != entry.name, "keep duplicate occurrences");
	auto manifest = Manifest{ "account", "123", "Chat", "complete", true };
	manifest.entries.insert(Key(entry), entry);
	Require(Write(folder, manifest).isEmpty(), "write manifest");
	Require(!Complete(folder, entry), "receipt before publish is incomplete");
	const auto partial = path("partial");
	const auto destination = path(entry.name);
	Put(partial, "abc");
	entry.sha256 = Checksum(partial);
	manifest.entries[Key(entry)] = entry;
	Require(Write(folder, manifest).isEmpty(), "persist receipt before publishing");
	Require(Publish(partial, destination, 3).isEmpty(), "publish complete download");
	Require(!QFile::exists(partial) && Complete(folder, entry), "atomic publication and receipt");
	auto read = Manifest{ "account", "123" };
	Require(Read(folder, read).isEmpty(), "resume manifest");
	Require(read.entries[Key(entry)].document == entry.document, "64-bit document identity roundtrip");
	Require(Complete(folder, read.entries[Key(entry)]), "recover crash after publish before UI update");
	auto otherAccount = Manifest{ "other", "123" };
	Require(!Read(folder, otherAccount).isEmpty(), "reject wrong account");
	auto otherChat = Manifest{ "account", "999" };
	Require(!Read(folder, otherChat).isEmpty(), "reject wrong chat");
	Put(destination, "abd");
	Require(!Complete(folder, entry), "detect same-size corruption");
	Put(destination, "ab");
	Require(!Complete(folder, entry), "detect truncated file");
	Put(destination, "abc");
	auto cancelled = std::atomic<bool>(false);
	const auto copy = path("copy.mp4");
	Require(Copy(destination, copy, 3, cancelled, entry.sha256).isEmpty(), "copy duplicate to separate file");
	Require(Get(copy) == "abc", "duplicate bytes preserved");
	Require(!Copy(destination, copy, 3, cancelled).isEmpty(), "never overwrite destination");
	Require(Get(copy) == "abc", "collision leaves destination intact");
	const auto wrongHash = path("wrong-hash.mp4");
	Require(!Copy(destination, wrongHash, 3, cancelled, QString(64, '0')).isEmpty(), "detect source changing during copy");
	Require(!QFile::exists(wrongHash), "discard invalid copy before commit");
	cancelled.store(true);
	const auto aborted = path("aborted.mp4");
	Require(!Copy(destination, aborted, 3, cancelled).isEmpty(), "cancel copy");
	Require(!QFile::exists(aborted), "cancel leaves no final file");
	cancelled.store(false);
	Require(!Copy(destination, aborted, 4, cancelled).isEmpty(), "reject wrong source size");
	Put(partial, "ab");
	Require(!Publish(partial, aborted, 3).isEmpty(), "reject incomplete network file");
	Require(!QFile::exists(aborted), "incomplete file is not published");
	Put(partial, "xyz");
	Require(!Publish(partial, copy, 3).isEmpty(), "publish never replaces user file");
	Require(Get(copy) == "abc", "failed publish preserves user file");
	const auto link = path("symlink");
	Require(QFile::link(destination, link), "create symlink fixture");
	auto linked = entry;
	linked.name = "symlink";
	Require(!Complete(folder, linked), "reject symbolic link completion");
	Require(Checksum(link).isEmpty(), "do not hash symlink targets");
	auto malicious = entry;
	malicious.name = "../outside";
	manifest.entries[Key(entry)] = malicious;
	Require(Write(folder, manifest).isEmpty(), "write traversal fixture");
	read.entries.clear();
	Require(!Read(folder, read).isEmpty(), "reject manifest path traversal");
	malicious.name = "C:outside";
	manifest.entries[Key(entry)] = malicious;
	Require(Write(folder, manifest).isEmpty(), "write drive fixture");
	Require(!Read(folder, read).isEmpty(), "reject Windows drive-relative paths");
	Put(path("video-export.json"), "invalid json");
	Require(!Read(folder, read).isEmpty(), "reject corrupt manifest");
	QTextStream(stdout) << "Video export file integrity, crash recovery, duplicates, cancellation and collision checks passed.\n";
}
