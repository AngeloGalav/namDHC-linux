// SPDX-License-Identifier: GPL-3.0-or-later
#include "chdinfo.h"

#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

namespace namdhc {

ChdInfo parseChdInfo(const QString &text)
{
	ChdInfo info;
	info.raw = text;

	static const QRegularExpression kv(R"(^([A-Za-z][^:]*):\s*(.*)$)");
	static const QRegularExpression tagRe(R"(Tag='(.{4})')");
	static const QRegularExpression divider(R"(^-{6,}\s+-{3,}\s+-{10,}\s*$)");
	static const QRegularExpression compRow(R"(^\s*([\d,]+)\s+([\d.]+%)\s+(.+?)\s*$)");

	bool inMetadata = false, inTable = false;
	for (QString line : text.split('\n')) {
		line.remove('\r');
		if (line.trimmed().isEmpty()) {
			inMetadata = false;
			continue;
		}
		if (line.contains("Compressed Hunks of Data"))
			continue;
		if (line.startsWith("Error")) {
			info.error = line.trimmed();
			continue;
		}
		if (divider.match(line).hasMatch()) {
			inTable = true;
			continue;
		}
		if (inTable) {
			const QRegularExpressionMatch m = compRow.match(line);
			if (m.hasMatch())
				info.compression << ChdInfo::CompressionRow{m.captured(1), m.captured(2), m.captured(3)};
			continue;
		}
		if (line.startsWith("Metadata:")) {
			inMetadata = true;
			const QString entry = line.mid(9).trimmed();
			const QRegularExpressionMatch t = tagRe.match(entry);
			if (t.hasMatch())
				info.metadataTags << t.captured(1);
			info.metadata += entry + "\n";
			continue;
		}
		if (inMetadata && line.startsWith(' ')) {
			QString data = line.trimmed();
			if (data.endsWith('.'))
				data.chop(1);
			if (data.isEmpty())
				continue;
			if (data.contains("TRACK:")) {
				data.replace(' ', " | ").replace(':', ": ");
				info.metadata += "    " + data + "\n\n";
			} else {
				info.metadata += "    " + data + "\n";
			}
			continue;
		}
		if (line.trimmed().startsWith("Hunks") && line.contains("Percent"))
			continue;
		const QRegularExpressionMatch m = kv.match(line);
		if (m.hasMatch())
			info.fields << qMakePair(m.captured(1).trimmed(), m.captured(2).trimmed());
	}
	info.ok = !info.fields.isEmpty() && info.error.isEmpty();
	return info;
}

ChdInfo readChdInfo(const QString &chdman, const QString &file, int timeoutMs)
{
	QProcess p;
	p.setProcessChannelMode(QProcess::MergedChannels);
	p.setWorkingDirectory(QFileInfo(file).absolutePath());
	p.start(chdman, {"info", "-v", "-i", file});
	if (!p.waitForStarted(5000)) {
		ChdInfo info;
		info.error = "Unable to run chdman (" + chdman + ")";
		return info;
	}
	p.waitForFinished(timeoutMs);
	ChdInfo info = parseChdInfo(QString::fromUtf8(p.readAll()));
	if (p.exitCode() != 0 && info.error.isEmpty())
		info.error = "chdman info failed with exit code " + QString::number(p.exitCode());
	if (!info.error.isEmpty())
		info.ok = false;
	return info;
}

QString mediaFromChdInfo(const ChdInfo &info)
{
	for (const QString &tag : info.metadataTags) {
		if (tag == "CHCD" || tag == "CHTR" || tag == "CHT2" || tag == "CHGT" || tag == "CHGD")
			return "cd";
		if (tag == "DVD ")
			return "dvd";
		if (tag == "AVAV" || tag == "AVLD")
			return "ld";
		if (tag == "GDDD" || tag == "IDNT")
			return "hd";
	}
	return "raw";
}

} // namespace namdhc
