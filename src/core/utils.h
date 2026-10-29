// SPDX-License-Identifier: GPL-3.0-or-later
// File, archive and formatting helpers.
#pragma once

#include <QString>
#include <QStringList>

namespace namdhc {

struct SplitPath {
	QString dir;    // directory without trailing slash
	QString file;   // name with extension
	QString noExt;  // name without extension
	QString ext;    // extension without dot
};
SplitPath splitFilePath(const QString &path);

// The descriptor itself plus every existing track file referenced by a CUE/TOC/GDI.
QStringList filesFromCueGdiToc(const QString &file);

// Recursively (or not) collect files under dir whose extension is in exts (case-insensitive).
QStringList scanFolder(const QString &dir, const QStringList &exts, bool recursive = true);

bool deleteFileWithRetry(const QString &file, int attempts = 5, int sleepMs = 50);
bool removeDirRecursively(const QString &dir);
bool isDirEmpty(const QString &dir);

QString formatBytes(qint64 bytes);        // "1.21 GB"
QString formatBytesIEC(qint64 bytes);     // "1.13 GiB"
QString formatBytesReport(qint64 bytes);  // "1.21 GB (1.13 GiB)"
QString millisecToTime(qint64 ms);        // "HH:MM:SS"
QString drawLine(int n = 77);

// External tools -------------------------------------------------------

// Locate the chdman executable: $NAMDHC_CHDMAN, the configured path, next to
// the running binary, ../libexec/namdhc, then $PATH.
QString locateChdman(const QString &configured = QString());

// Locate a 7-Zip executable (7z, 7zz, 7za): configured path or $PATH.
QString locate7Zip(const QString &configured = QString());
QString locateUnzip();

struct ArchiveEntry {
	QString path;
	qint64 size = -1;
};

// List the file entries of a zip/7z archive using the available tools.
QList<ArchiveEntry> listArchiveEntries(const QString &archive, const QString &sevenZip);
QStringList listArchive(const QString &archive, const QString &sevenZip);

// Returns the first entry in the archive whose extension is in exts (archives excluded).
QString firstMatchingArchiveEntry(const QString &archive, const QStringList &exts, const QString &sevenZip);

// True if zip/7z input can be handled with the tools found on the system.
bool canHandleArchive(const QString &ext, const QString &sevenZip);

} // namespace namdhc
