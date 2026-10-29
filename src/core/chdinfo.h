// SPDX-License-Identifier: GPL-3.0-or-later
// Parsing of `chdman info -v` output.
#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace namdhc {

struct ChdInfo {
	struct CompressionRow {
		QString hunks;
		QString percent;
		QString name;
	};

	bool ok = false;
	QString raw;
	QList<QPair<QString, QString>> fields;  // "File Version" -> "5", ...
	QList<CompressionRow> compression;
	QStringList metadataTags;               // "CHT2", "DVD ", ...
	QString metadata;                       // human readable metadata block
	QString error;
};

ChdInfo parseChdInfo(const QString &text);

// Runs `chdman info -v -i file` synchronously.
ChdInfo readChdInfo(const QString &chdman, const QString &file, int timeoutMs = 60000);

// Guess the media of a CHD from its metadata: "cd", "dvd", "hd", "ld" or "raw".
QString mediaFromChdInfo(const ChdInfo &info);

} // namespace namdhc
