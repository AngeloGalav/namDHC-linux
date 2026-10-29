// SPDX-License-Identifier: GPL-3.0-or-later
#include "utils.h"

#include "options.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>

namespace namdhc {

SplitPath splitFilePath(const QString &path)
{
	const QFileInfo fi(path);
	SplitPath p;
	p.dir = fi.absolutePath();
	p.file = fi.fileName();
	p.ext = fi.suffix();
	p.noExt = p.ext.isEmpty() ? p.file : p.file.left(p.file.size() - p.ext.size() - 1);
	return p;
}

QStringList filesFromCueGdiToc(const QString &file)
{
	QStringList list;
	if (!QFileInfo::exists(file))
		return list;
	list << file;

	const SplitPath f = splitFilePath(file);
	const QString ext = f.ext.toLower();
	if (ext != "cue" && ext != "toc" && ext != "gdi")
		return list;

	QFile in(file);
	if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
		return list;

	static const QRegularExpression quoted(R"re(^\s*FILE\s+"([^"]+)")re", QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression bare(R"(^\s*FILE\s+(\S+))", QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression gdiLine(R"(^\s*\d)");
	static const QRegularExpression gdiQuoted(R"re("([^"]+)")re");

	auto addIfExists = [&](const QString &name) {
		if (name.isEmpty())
			return;
		const QString full = QDir(f.dir).filePath(name);
		if (QFileInfo(full).isFile() && !list.contains(full))
			list << full;
	};

	QTextStream ts(&in);
	int lineNo = 0;
	while (!ts.atEnd()) {
		const QString line = ts.readLine();
		++lineNo;
		if (ext == "cue" || ext == "toc") {
			QRegularExpressionMatch m = quoted.match(line);
			if (m.hasMatch())
				addIfExists(m.captured(1));
			else if ((m = bare.match(line)).hasMatch())
				addIfExists(m.captured(1));
		} else if (lineNo > 1 && gdiLine.match(line).hasMatch()) {
			// GDI: "track lba type sectorsize filename offset"; filenames may be quoted
			const QRegularExpressionMatch q = gdiQuoted.match(line);
			if (q.hasMatch()) {
				addIfExists(q.captured(1));
			} else {
				for (const QString &tok : line.split(QRegularExpression("[\\s;]+"), Qt::SkipEmptyParts))
					if (QFileInfo(QDir(f.dir).filePath(tok)).isFile())
						addIfExists(tok);
			}
		}
	}
	return list;
}

QStringList scanFolder(const QString &dir, const QStringList &exts, bool recursive)
{
	QStringList filters;
	for (const QString &e : exts)
		filters << "*." + e;
	QStringList out;
	QDirIterator it(dir, filters, QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
		recursive ? QDirIterator::Subdirectories | QDirIterator::FollowSymlinks : QDirIterator::NoIteratorFlags);
	while (it.hasNext())
		out << QFileInfo(it.next()).absoluteFilePath();
	// QDir name filters are case-insensitive on most setups but not guaranteed; filter again.
	QStringList filtered;
	for (const QString &f : out)
		if (exts.contains(QFileInfo(f).suffix(), Qt::CaseInsensitive))
			filtered << f;
	filtered.sort(Qt::CaseInsensitive);
	return filtered;
}

bool deleteFileWithRetry(const QString &file, int attempts, int sleepMs)
{
	QFileInfo fi(file);
	if (!fi.exists() || fi.isDir())
		return true;
	for (int i = 0; i < qMax(1, attempts); ++i) {
		QFile f(file);
		f.setPermissions(f.permissions() | QFileDevice::WriteOwner);
		if (f.remove() || !QFileInfo::exists(file))
			return true;
		QThread::msleep(sleepMs);
	}
	return !QFileInfo::exists(file);
}

bool removeDirRecursively(const QString &dir)
{
	QDir d(dir);
	if (!d.exists())
		return true;
	return d.removeRecursively();
}

bool isDirEmpty(const QString &dir)
{
	return QDir(dir).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
}

QString formatBytes(qint64 n)
{
	// Decimal units, like the Windows shell does for namDHC reports.
	const char *units[] = {"bytes", "KB", "MB", "GB", "TB", "PB"};
	double v = double(n);
	int i = 0;
	while (std::abs(v) >= 1000.0 && i < 5) {
		v /= 1000.0;
		++i;
	}
	if (i == 0)
		return QString::number(n) + " bytes";
	return QString::number(v, 'f', std::abs(v) >= 100 ? 0 : std::abs(v) >= 10 ? 1 : 2) + " " + units[i];
}

QString formatBytesIEC(qint64 n)
{
	const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
	double v = double(n);
	int i = 0;
	while (std::abs(v) >= 1024.0 && i < 5) {
		v /= 1024.0;
		++i;
	}
	if (i == 0)
		return QString::number(n) + " B";
	QString s = QString::number(v, 'f', std::abs(v) >= 100 ? 0 : std::abs(v) >= 10 ? 1 : 2);
	if (s.contains('.')) {
		while (s.endsWith('0'))
			s.chop(1);
		if (s.endsWith('.'))
			s.chop(1);
	}
	return s + " " + units[i];
}

QString formatBytesReport(qint64 n)
{
	return formatBytes(n) + " (" + formatBytesIEC(n) + ")";
}

QString millisecToTime(qint64 ms)
{
	const qint64 secs = (ms / 1000) % 60;
	const qint64 mins = (ms / 60000) % 60;
	const qint64 hours = ms / 3600000;
	return QString("%1:%2:%3").arg(hours, 2, 10, QChar('0')).arg(mins, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0'));
}

QString drawLine(int n)
{
	return QString(qMax(0, n), '-');
}

static bool isExecutableFile(const QString &path)
{
	const QFileInfo fi(path);
	return fi.isFile() && fi.isExecutable();
}

QString locateChdman(const QString &configured)
{
	const QString env = qEnvironmentVariable("NAMDHC_CHDMAN");
	if (!env.isEmpty() && isExecutableFile(env))
		return env;
	if (!configured.isEmpty() && isExecutableFile(configured))
		return configured;

	const QString appDir = QCoreApplication::applicationDirPath();
	for (const QString &candidate : {appDir + "/chdman", appDir + "/../libexec/namdhc/chdman", appDir + "/../lib/namdhc/chdman"}) {
		if (isExecutableFile(candidate))
			return QFileInfo(candidate).canonicalFilePath();
	}
	return QStandardPaths::findExecutable("chdman");
}

QString locate7Zip(const QString &configured)
{
	if (!configured.isEmpty()) {
		QFileInfo fi(configured);
		if (fi.isDir()) {
			for (const char *n : {"7z", "7zz", "7za"})
				if (isExecutableFile(fi.absoluteFilePath() + "/" + n))
					return fi.absoluteFilePath() + "/" + n;
		} else if (isExecutableFile(configured)) {
			return configured;
		}
	}
	for (const char *n : {"7z", "7zz", "7za"}) {
		const QString p = QStandardPaths::findExecutable(n);
		if (!p.isEmpty())
			return p;
	}
	return {};
}

QString locateUnzip()
{
	return QStandardPaths::findExecutable("unzip");
}

bool canHandleArchive(const QString &ext, const QString &sevenZip)
{
	const QString e = ext.toLower();
	if (e == "zip")
		return !sevenZip.isEmpty() || !locateUnzip().isEmpty();
	if (e == "7z")
		return !sevenZip.isEmpty();
	return false;
}

static QString runAndCapture(const QString &program, const QStringList &args, int timeoutMs = 60000)
{
	QProcess p;
	p.setProcessChannelMode(QProcess::MergedChannels);
	p.start(program, args);
	if (!p.waitForStarted(5000))
		return {};
	p.waitForFinished(timeoutMs);
	if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
		return {};
	return QString::fromUtf8(p.readAll());
}

QList<ArchiveEntry> listArchiveEntries(const QString &archive, const QString &sevenZip)
{
	QList<ArchiveEntry> items;
	const QString ext = QFileInfo(archive).suffix().toLower();
	if (!sevenZip.isEmpty()) {
		// 7z -slt prints one "Key = Value" block per entry after a "----------" divider.
		const QString out = runAndCapture(sevenZip, {"l", "-slt", "-sccUTF-8", "--", archive});
		bool sawDivider = false;
		bool isDir = false;
		ArchiveEntry cur;
		auto flush = [&] {
			if (!cur.path.isEmpty() && !isDir)
				items << cur;
			cur = ArchiveEntry();
			isDir = false;
		};
		for (const QString &raw : out.split('\n')) {
			const QString line = raw.trimmed();
			if (line == "----------") {
				sawDivider = true;
				continue;
			}
			if (!sawDivider)
				continue;
			if (line.startsWith("Path = ")) {
				flush();
				cur.path = line.mid(7);
			} else if (line.startsWith("Size = ")) {
				cur.size = line.mid(7).toLongLong();
			} else if (line.startsWith("Folder = +") || (line.startsWith("Attributes = ") && line.mid(13).startsWith('D'))) {
				isDir = true;
			}
		}
		flush();
		return items;
	}
	if (ext == "zip") {
		const QString unzip = locateUnzip();
		if (!unzip.isEmpty()) {
			// "unzip -l": "  Length      Date    Time    Name" rows between two dashed lines.
			static const QRegularExpression row(R"(^\s*(\d+)\s+\S+\s+\S+\s+(.+)$)");
			const QString out = runAndCapture(unzip, {"-l", archive});
			int dashes = 0;
			for (const QString &line : out.split('\n')) {
				if (line.trimmed().startsWith("---")) {
					++dashes;
					continue;
				}
				if (dashes != 1)
					continue;
				const QRegularExpressionMatch m = row.match(line);
				if (m.hasMatch() && !m.captured(2).endsWith('/'))
					items << ArchiveEntry{m.captured(2), m.captured(1).toLongLong()};
			}
		}
	}
	return items;
}

QStringList listArchive(const QString &archive, const QString &sevenZip)
{
	QStringList names;
	for (const ArchiveEntry &e : listArchiveEntries(archive, sevenZip))
		names << e.path;
	return names;
}

QString firstMatchingArchiveEntry(const QString &archive, const QStringList &exts, const QString &sevenZip)
{
	for (const QString &entry : listArchive(archive, sevenZip)) {
		const QString e = QFileInfo(entry).suffix().toLower();
		if (!isArchiveExt(e) && exts.contains(e, Qt::CaseInsensitive))
			return entry;
	}
	return {};
}

int compareVersions(const QString &a, const QString &b)
{
	static const QRegularExpression nonDigits("[^0-9.]");
	const QStringList pa = QString(a).remove(nonDigits).split('.', Qt::SkipEmptyParts);
	const QStringList pb = QString(b).remove(nonDigits).split('.', Qt::SkipEmptyParts);
	for (int i = 0; i < qMax(pa.size(), pb.size()); ++i) {
		const int va = i < pa.size() ? pa[i].toInt() : 0;
		const int vb = i < pb.size() ? pb[i].toInt() : 0;
		if (va != vb)
			return va < vb ? -1 : 1;
	}
	return 0;
}

} // namespace namdhc
