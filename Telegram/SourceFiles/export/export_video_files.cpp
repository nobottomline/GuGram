#include "export/export_video_files.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QSaveFile>
#include <QtCore/QRegularExpression>
#include <QtCore/QCryptographicHash>

namespace Export::VideoFiles {
namespace {

QString ManifestPath(const QString &folder) {
	return QDir(folder).filePath(QStringLiteral("video-export.json"));
}

bool ValidName(const QString &name) {
	return !name.isEmpty()
		&& name != QStringLiteral(".")
		&& name != QStringLiteral("..")
		&& !name.contains('/')
		&& !name.contains('\\')
		&& !name.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f<>:\"|?*]")))
		&& !QDir::isAbsolutePath(name);
}

} // namespace

QString Key(const Entry &entry) {
	return entry.peer + ':' + entry.message + ':' + entry.document;
}

QString SafeName(QString name) {
	name.replace(QRegularExpression(QStringLiteral("[\\x00-\\x1f<>:\"/\\\\|?*]")), QStringLiteral("_"));
	name = name.trimmed();
	while (name.toUtf8().size() > 100) {
		const auto last = QChar(name.back());
		name.chop(1);
		if (last.isLowSurrogate() && !name.isEmpty() && name.back().isHighSurrogate()) {
			name.chop(1);
		}
	}
	while (name.endsWith('.') || name.endsWith(' ')) {
		name.chop(1);
	}
	return name.isEmpty() ? QStringLiteral("video") : name;
}

QString FileName(const Entry &entry, const QString &original) {
	auto suffix = QFileInfo(original).suffix();
	if (!QRegularExpression(QStringLiteral("^[a-zA-Z0-9]{1,10}$")).match(suffix).hasMatch()) {
		suffix = QStringLiteral("mp4");
	}
	const auto name = SafeName(QFileInfo(original).completeBaseName()) + '.' + suffix;
	return entry.peer + '_' + entry.message + '_' + entry.document + '_' + name;
}

QString Read(const QString &folder, Manifest &manifest) {
	auto file = QFile(ManifestPath(folder));
	if (QFileInfo(file).isSymLink()) {
		return QStringLiteral("The export manifest must not be a symbolic link.");
	}
	if (!file.exists()) {
		return {};
	}
	if (!file.open(QIODevice::ReadOnly)) {
		return file.errorString();
	}
	auto error = QJsonParseError();
	const auto json = QJsonDocument::fromJson(file.readAll(), &error);
	const auto root = json.object();
	if (error.error != QJsonParseError::NoError
		|| root.value(QStringLiteral("schema")).toInt() != 1
		|| root.value(QStringLiteral("account")).toString() != manifest.account
		|| root.value(QStringLiteral("peer")).toString() != manifest.peer) {
		return QStringLiteral("The export manifest is invalid or belongs to another account/chat.");
	}
	for (const auto value : root.value(QStringLiteral("videos")).toArray()) {
		const auto object = value.toObject();
		auto entry = Entry{
			object.value(QStringLiteral("peer")).toString(),
			object.value(QStringLiteral("message")).toString(),
			object.value(QStringLiteral("document")).toString(),
			object.value(QStringLiteral("file")).toString(),
			object.value(QStringLiteral("bytes")).toString().toLongLong(),
			object.value(QStringLiteral("status")).toString(),
			object.value(QStringLiteral("error")).toString(),
			object.value(QStringLiteral("sha256")).toString(),
		};
		if (!ValidName(entry.name) || entry.size <= 0) {
			return QStringLiteral("The export manifest contains an invalid file.");
		}
		manifest.entries.insert(Key(entry), std::move(entry));
	}
	return {};
}

QString Write(const QString &folder, const Manifest &manifest) {
	auto videos = QJsonArray();
	for (const auto &entry : manifest.entries) {
		videos.append(QJsonObject{
			{ QStringLiteral("peer"), entry.peer },
			{ QStringLiteral("message"), entry.message },
			{ QStringLiteral("document"), entry.document },
			{ QStringLiteral("file"), entry.name },
			{ QStringLiteral("bytes"), QString::number(entry.size) },
			{ QStringLiteral("status"), entry.status },
			{ QStringLiteral("error"), entry.error },
			{ QStringLiteral("sha256"), entry.sha256 },
		});
	}
	const auto root = QJsonObject{
		{ QStringLiteral("schema"), 1 },
		{ QStringLiteral("account"), manifest.account },
		{ QStringLiteral("peer"), manifest.peer },
		{ QStringLiteral("title"), manifest.title },
		{ QStringLiteral("state"), manifest.state },
		{ QStringLiteral("scan_complete"), manifest.scanComplete },
		{ QStringLiteral("unavailable_media"), manifest.unavailable },
		{ QStringLiteral("videos"), videos },
	};
	auto file = QSaveFile(ManifestPath(folder));
	if (QFileInfo(file.fileName()).isSymLink()) {
		return QStringLiteral("The export manifest must not be a symbolic link.");
	}
	const auto bytes = QJsonDocument(root).toJson();
	if (!file.open(QIODevice::WriteOnly)
		|| file.write(bytes) != bytes.size()
		|| !file.commit()) {
		return file.errorString();
	}
	return {};
}

QString Checksum(const QString &path, const std::atomic<bool> *cancelled) {
	auto file = QFile(path);
	if (QFileInfo(file).isSymLink() || !file.open(QIODevice::ReadOnly)) {
		return {};
	}
	auto hash = QCryptographicHash(QCryptographicHash::Sha256);
	while (!file.atEnd()) {
		if (cancelled && cancelled->load()) {
			return {};
		}
		const auto bytes = file.read(1024 * 1024);
		if (bytes.isEmpty()) {
			return {};
		}
		hash.addData(bytes);
	}
	return QString::fromLatin1(hash.result().toHex());
}

bool Complete(
		const QString &folder,
		const Entry &entry,
		const std::atomic<bool> *cancelled) {
	const auto file = QFileInfo(QDir(folder).filePath(entry.name));
	return ValidName(entry.name)
		&& entry.status == QStringLiteral("complete")
		&& file.isFile()
		&& !file.isSymLink()
		&& file.size() == entry.size
		&& entry.sha256.size() == 64
		&& Checksum(file.filePath(), cancelled) == entry.sha256;
}

QString Copy(
		const QString &source,
		const QString &destination,
		qint64 size,
		const std::atomic<bool> &cancelled,
		const QString &expectedSha256) {
	if (QFileInfo::exists(destination)) {
		return QStringLiteral("Destination already exists; it was not overwritten.");
	}
	auto input = QFile(source);
	auto output = QSaveFile(destination);
	if (!input.open(QIODevice::ReadOnly) || input.size() != size) {
		return QStringLiteral("The reusable file is missing or incomplete.");
	}
	if (!output.open(QIODevice::WriteOnly)) {
		return output.errorString();
	}
	auto copied = qint64(0);
	auto hash = QCryptographicHash(QCryptographicHash::Sha256);
	while (copied < size && !cancelled.load()) {
		const auto bytes = input.read(1024 * 1024);
		if (bytes.isEmpty()) {
			return QStringLiteral("Could not read the complete source file.");
		}
		if (output.write(bytes) != bytes.size()) {
			return output.errorString();
		}
		copied += bytes.size();
		hash.addData(bytes);
	}
	if (cancelled.load()) {
		return QStringLiteral("Cancelled");
	}
	if (!expectedSha256.isEmpty()
		&& QString::fromLatin1(hash.result().toHex()) != expectedSha256) {
		return QStringLiteral("The source video changed while copying.");
	}
	if (copied != size || !output.commit()) {
		return QStringLiteral("Could not commit the complete video.");
	}
	return {};
}

QString Publish(
		const QString &partial,
		const QString &destination,
		qint64 size) {
	const auto info = QFileInfo(partial);
	if (!info.isFile() || info.isSymLink() || info.size() != size) {
		return QStringLiteral("The downloaded file is incomplete.");
	}
	if (QFileInfo::exists(destination)) {
		return QStringLiteral("Destination already exists; it was not overwritten.");
	}
	if (!QFile::rename(partial, destination)) {
		return QStringLiteral("Could not publish the downloaded file.");
	}
	return {};
}

} // namespace Export::VideoFiles
