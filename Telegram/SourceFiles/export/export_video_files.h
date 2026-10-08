#pragma once

#include <QtCore/QString>
#include <QtCore/QJsonObject>
#include <QtCore/QMap>
#include <atomic>

namespace Export::VideoFiles {

struct Entry {
	QString peer;
	QString message;
	QString document;
	QString name;
	qint64 size = 0;
	QString status;
	QString error;
	QString sha256;
};

struct Manifest {
	QString account;
	QString peer;
	QString title;
	QString state;
	bool scanComplete = false;
	int unavailable = 0;
	QMap<QString, Entry> entries;
};

QString Key(const Entry &entry);
QString FileName(const Entry &entry, const QString &original);
QString SafeName(QString name);
QString Read(const QString &folder, Manifest &manifest);
QString Write(const QString &folder, const Manifest &manifest);
bool Complete(
	const QString &folder,
	const Entry &entry,
	const std::atomic<bool> *cancelled = nullptr);
QString Checksum(const QString &path, const std::atomic<bool> *cancelled = nullptr);
QString Copy(
	const QString &source,
	const QString &destination,
	qint64 size,
	const std::atomic<bool> &cancelled,
	const QString &expectedSha256 = {});
QString Publish(const QString &partial, const QString &destination, qint64 size);

} // namespace Export::VideoFiles
