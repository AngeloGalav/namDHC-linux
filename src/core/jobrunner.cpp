// SPDX-License-Identifier: GPL-3.0-or-later
#include "jobrunner.h"

#include "utils.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>

#include <csignal>
#include <sys/prctl.h>
#include <unistd.h>

namespace namdhc {

QString jobStatusName(JobStatus s)
{
	switch (s) {
	case JobStatus::Pending: return "pending";
	case JobStatus::Starting: return "starting";
	case JobStatus::Unzipping: return "unzipping";
	case JobStatus::Running: return "running";
	case JobStatus::Success: return "success";
	case JobStatus::Error: return "error";
	case JobStatus::FileExists: return "fileExists";
	case JobStatus::Cancelled: return "cancelled";
	}
	return {};
}

static QString shellQuote(const QString &s)
{
	static const QRegularExpression safe("^[A-Za-z0-9_@%+=:,./-]+$");
	if (safe.match(s).hasMatch())
		return s;
	QString q = s;
	q.replace("'", "'\\''");
	return "'" + q + "'";
}

// Put the child in its own process group: a Ctrl+C in the terminal then only
// reaches namdhc, which cancels the job and cleans up partial output itself.
static void detachFromTerminalSignals(QProcess *p)
{
	p->setChildProcessModifier([] {
		::setpgid(0, 0);
		::prctl(PR_SET_PDEATHSIG, SIGTERM);  // never outlive namdhc
	});
}

static QString formatReportList(const QString &title, const QStringList &items)
{
	QString out = title + "\n";
	for (const QString &i : items)
		out += "    " + i + "\n";
	return out;
}

JobRunner::JobRunner(const Job &job, const RunnerConfig &cfg, QObject *parent)
	: QObject(parent), m_job(job), m_cfg(cfg)
{
	m_killTimer.setSingleShot(true);
	connect(&m_killTimer, &QTimer::timeout, this, [this] {
		if (m_proc && m_proc->state() != QProcess::NotRunning)
			m_proc->kill();
	});
}

JobRunner::~JobRunner()
{
	if (m_proc && m_proc->state() != QProcess::NotRunning) {
		m_proc->disconnect(this);
		m_proc->kill();
		m_proc->waitForFinished(2000);
	}
	if (!m_tempDir.isEmpty())
		removeDirRecursively(m_tempDir);
}

QString JobRunner::report() const
{
	return "\n\n" + m_job.cmd.toUpper() + " - " + m_job.workingTitle + "\n" + drawLine(77) + "\n" + m_report;
}

void JobRunner::setProgress(int percent, const QString &text, bool busy)
{
	m_percent = qBound(0, percent, 100);
	emit progress(m_percent, text, busy);
}

void JobRunner::addReport(const QString &text)
{
	m_report += text;
}

void JobRunner::log(const QString &msg)
{
	if (msg.isEmpty())
		return;
	m_lastLog = msg;
	emit logMessage(msg);
}

void JobRunner::start()
{
	if (m_cancelRequested) {
		finishCancelled();
		return;
	}
	m_status = JobStatus::Starting;
	log("Preparing " + m_job.cmd.toUpper() + " - " + m_job.workingTitle);
	setProgress(0, "Preparing  -  " + m_job.workingTitle);
	emit output("Starting a " + m_job.cmd.toUpper() + " job\n\nWorking job file: " + m_job.workingTitle
		+ "\nWorking directory: " + m_job.workingDir + "\n");

	if (!m_job.outputFolder.isEmpty() && !QFileInfo(m_job.outputFolder).isDir()) {
		if (QDir().mkpath(m_job.outputFolder)) {
			m_outputFolderCreated = true;
			emit output("Created directory " + m_job.outputFolder + "\n");
		} else {
			failAndFinish("Error creating directory " + m_job.outputFolder,
				"\nError creating directory " + m_job.outputFolder + "\n",
				"Error creating directory  -  " + m_job.workingTitle);
			return;
		}
	}

	if (isArchiveExt(m_job.fromFileExt)) {
		startArchiveExtraction();
		return;
	}
	m_inputFile = m_job.fromFileFull;
	startChdman();
}

// ------------------------------------------------------------------ archives

void JobRunner::startArchiveExtraction()
{
	m_status = JobStatus::Unzipping;
	const bool isZip = m_job.fromFileExt.toLower() == "zip";
	const QString action = isZip ? "Unzipping" : "Extracting archive";
	setProgress(0, action + "  -  " + m_job.fromFile, true);

	m_tempDir = m_cfg.tempDir + "/" + QString::number(m_job.idx) + "-" + m_job.fromFileNoExt;
	removeDirRecursively(m_tempDir);
	if (!QDir().mkpath(m_tempDir)) {
		failAndFinish("Error creating temporary directory '" + m_tempDir + "'",
			"\nError creating temporary directory '" + m_tempDir + "'\n",
			"Error creating temp directory  -  " + m_job.workingTitle);
		return;
	}

	QString program;
	QStringList args;
	if (!m_cfg.sevenZip.isEmpty()) {
		program = m_cfg.sevenZip;
		args = {"x", "-y", "-bd", "-bsp1", "-o" + m_tempDir, "--", m_job.fromFileFull};
	} else if (isZip && !locateUnzip().isEmpty()) {
		program = locateUnzip();
		args = {"-o", "-qq", m_job.fromFileFull, "-d", m_tempDir};
	} else {
		failAndFinish("Error extracting archive file '" + m_job.fromFileFull + "' - 7-Zip not found",
			"\nError extracting archive file '" + m_job.fromFileFull + "': no 7z/unzip tool found\n",
			"Error extracting archive  -  " + m_job.fromFile);
		return;
	}

	emit output(action + " " + m_job.fromFileFull + "\nExtracting to: " + m_tempDir + "\n");
	m_proc = new QProcess(this);
	m_proc->setProcessChannelMode(QProcess::MergedChannels);
	detachFromTerminalSignals(m_proc);
	connect(m_proc, &QProcess::readyRead, this, &JobRunner::onArchiveOutput);
	connect(m_proc, &QProcess::finished, this, &JobRunner::onArchiveFinished);
	connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart)
			onArchiveFinished(-1, QProcess::CrashExit);
	});
	m_proc->start(program, args);
}

void JobRunner::onArchiveOutput()
{
	const QString data = QString::fromUtf8(m_proc->readAll());
	emit output(data);
	static const QRegularExpression pct("(\\d{1,3})%");
	QRegularExpressionMatchIterator it = pct.globalMatch(data);
	int last = -1;
	while (it.hasNext())
		last = it.next().captured(1).toInt();
	if (last >= 0) {
		const QString action = m_job.fromFileExt.toLower() == "zip" ? "Unzipping" : "Extracting archive";
		setProgress(qMin(last, 99), action + "  -  " + m_job.fromFile + "  -  " + QString::number(last) + "%", false);
	}
}

void JobRunner::onArchiveFinished(int exitCode, QProcess::ExitStatus st)
{
	if (!m_proc)
		return;
	m_proc->deleteLater();
	m_proc = nullptr;
	m_killTimer.stop();

	if (m_cancelRequested) {
		finishCancelled();
		return;
	}

	const bool isZip = m_job.fromFileExt.toLower() == "zip";
	const QString done = isZip ? "Unzipped" : "Extracted archive";
	const QString found = (st == QProcess::NormalExit && exitCode == 0) ? findExtractedInput() : QString();
	if (found.isEmpty()) {
		failAndFinish("Error extracting archive file '" + m_job.fromFileFull + "'",
			"\nError extracting archive file '" + m_job.fromFileFull + "'\n",
			"Error extracting archive  -  " + m_job.fromFile + "  -  " + m_job.workingTitle);
		return;
	}
	log(done + " " + m_job.fromFileFull + " successfully");
	addReport(done + " " + m_job.fromFile + " successfully\n");
	setProgress(100, done + " successfully  -  " + m_job.workingTitle);
	m_inputFile = found;
	startChdman();
}

QString JobRunner::findExtractedInput() const
{
	// Prefer the selected types in order (so a .cue wins over the .bin/.iso next to it).
	QStringList files;
	QDirIterator it(m_tempDir, QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext())
		files << it.next();
	files.sort(Qt::CaseInsensitive);
	for (const QString &ext : m_job.inputFileTypes) {
		if (isArchiveExt(ext))
			continue;
		for (const QString &f : files)
			if (QFileInfo(f).suffix().compare(ext, Qt::CaseInsensitive) == 0)
				return f;
	}
	return {};
}

// ------------------------------------------------------------------ chdman

void JobRunner::startChdman()
{
	if (m_cancelRequested) {
		finishCancelled();
		return;
	}

	QStringList args;
	args << m_job.cmd << m_job.cmdArgs;
	if (!m_inputFile.isEmpty())
		args << "-i" << m_inputFile;
	if (!m_job.toFileFull.isEmpty())
		args << "-o" << m_job.toFileFull;
	if (m_job.cmd == "info")
		args << "-v";

	QStringList quoted{shellQuote(m_cfg.chdman)};
	for (const QString &a : args)
		quoted << shellQuote(a);
	m_commandLine = quoted.join(' ');
	emit output("\nCommand line: " + m_commandLine + "\n\n");

	if (!m_job.toFileFull.isEmpty() && !m_job.force && QFileInfo::exists(m_job.toFileFull)) {
		log("file already exists - skipping");
		QString rep = "Warning: file already exists - skipping:\n\n" + m_job.toFileFull + "\nUse Force (-f) to overwrite.\n";
		if (m_job.deleteInputFiles)
			rep += "Input files were not deleted because the job was skipped.\n";
		addReport(rep);
		setProgress(100, "File already exists - Skipping  -  " + m_job.workingTitle);
		cleanupCreatedOutputFolder();
		finish(JobStatus::FileExists);
		return;
	}

	// Starting size: descriptor plus referenced tracks for CUE/GDI/TOC.
	m_startSize = 0;
	for (const QString &f : filesFromCueGdiToc(m_inputFile))
		m_startSize += QFileInfo(f).size();

	m_status = JobStatus::Running;
	log("Starting " + m_job.cmd.toUpper() + " - " + m_job.workingTitle);
	setProgress(0, "Starting job  -  " + m_job.workingTitle);

	m_proc = new QProcess(this);
	m_proc->setProcessChannelMode(QProcess::MergedChannels);
	m_proc->setWorkingDirectory(m_job.workingDir);
	detachFromTerminalSignals(m_proc);
	connect(m_proc, &QProcess::readyRead, this, &JobRunner::onChdmanOutput);
	connect(m_proc, &QProcess::finished, this, &JobRunner::onChdmanFinished);
	connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart && m_proc) {
			m_proc->deleteLater();
			m_proc = nullptr;
			failAndFinish("Error: Failed to launch CHDMAN process",
				"Error: Failed to launch CHDMAN process.\n\nCommand: " + m_commandLine + "\n",
				"Error launching CHDMAN  -  " + m_job.workingTitle);
		}
	});
	m_proc->start(m_cfg.chdman, args);
}

void JobRunner::onChdmanOutput()
{
	if (!m_proc)
		return;
	const QString data = QString::fromUtf8(m_proc->readAll());
	emit output(data);
	m_partial += data;
	// chdman ends progress lines with '\r' and everything else with '\n'.
	qsizetype start = 0;
	for (qsizetype i = 0; i < m_partial.size(); ++i) {
		const QChar c = m_partial.at(i);
		if (c == '\r' || c == '\n') {
			parseChdmanSegment(m_partial.mid(start, i - start));
			start = i + 1;
		}
	}
	m_partial = m_partial.mid(start);
}

void JobRunner::parseChdmanSegment(const QString &segment)
{
	const QString line = segment.trimmed();
	if (line.isEmpty())
		return;

	static const QRegularExpression progressRe(R"(^(Compressing|Extracting|Verifying|Examining parent),\s*([\d.]+)%\s*complete\.*\s*(\(ratio=[\d.]+%\))?)");
	const QRegularExpressionMatch m = progressRe.match(line);
	if (m.hasMatch()) {
		const QString what = m.captured(1);
		const double pct = m.captured(2).toDouble();
		const QString name = what == "Verifying" ? m_job.fromFile : m_job.toFile;
		QString text = what + "  -  " + name;
		if (pct > 0)
			text += "  -  " + m.captured(2) + "%";
		if (!m.captured(3).isEmpty())
			text += "  " + m.captured(3);
		setProgress(int(pct), text);
		return;
	}
	if (line.startsWith("Compression complete") || line.startsWith("Extraction complete")) {
		setProgress(100, line + "  -  " + m_job.workingTitle);
	}
	static const QRegularExpression logicalRe(R"(^(Logical size|Input file size):\s*([\d,]+))");
	const QRegularExpressionMatch lm = logicalRe.match(line);
	if (lm.hasMatch()) {
		m_logicalSize = QString(lm.captured(2)).remove(',').toLongLong();
		// chdman spins forever ("Compressing, -nan%") when the input has no data,
		// e.g. a CUE without valid tracks. Stop it and report an error instead.
		if (m_logicalSize == 0 && m_job.cmd.startsWith("create") && m_proc) {
			m_emptyInput = true;
			m_proc->kill();
		}
	}
	if (line.startsWith("Compressing,"))
		return;
	m_chdmanText += line + "\n";
}

QString JobRunner::checkForErrors(const QString &text) const
{
	static const QStringList errors = {
		"Error parsing input file", "Error: file already exists", "Error opening input file", "Error reading input file",
		"Unable to open file", "Error writing file", "Error opening parent CHD file", "CHD is uncompressed",
		"No verification to be done; CHD has no checksum", "Error reading CHD file", "Error creating CHD file",
		"Error opening CHD file", "Error during compression", "Invalid compressor", "Invalid hunk size",
		"Unit size is not an even divisor of the hunk size", "Unsupported version", "Error getting info on hunk",
		"Input start offset greater than input file size", "Can't guess CHS values because there is no input file",
		"Sector size does not apply", "Error reading audio samples", "Error assembling data for frame",
		"Invalid size string", "Error opening AVI file", "Error reading AVI frame", "CHD has no checksum",
		"Blank hard disks must be uncompressed", "CHS does not apply when creating a diff from the parent",
		"Invalid CHS string", "Error reading ident file", "Unable to find hard disk metadata in parent CHD",
		"Error parsing hard disk metadata in parent CHD", "Data size is not divisible by sector size",
		"Blank hard drives must specify either a length or a set of CHS values", "Error adding hard disk metadata",
		"Error adding CD metadata", "Uncompressed is not supported", "Error adding AV metadata",
		"Error adding AVLD metadata", "Hunk size is not an even multiple or divisor of input hunk size",
		"Error writing cloned metadata", "Error upgrading CD metadata", "Error writing upgraded CD metadata",
		"Error writing to file; check disk space", "Unable to recognize CHD file as a CD", "Error writing frame",
		"Unable to find A/V metadata in the input CHD", "Improperly formatted A/V metadata found",
		"Frame size does not match hunk size for this CHD", "Error reading hunk", "Error writing samples for hunk",
		"Error writing video for hunk", "Error reading metadata file", "Error adding metadata",
		"Error removing metadata:", "Error reading metadata:",
	};
	for (const QString &e : errors)
		if (text.contains(e))
			return e;
	return {};
}

void JobRunner::onChdmanFinished(int exitCode, QProcess::ExitStatus st)
{
	if (!m_proc)
		return;
	if (!m_partial.isEmpty()) {
		parseChdmanSegment(m_partial);
		m_partial.clear();
	}
	m_proc->deleteLater();
	m_proc = nullptr;
	m_killTimer.stop();

	if (m_cancelRequested) {
		finishCancelled();
		return;
	}

	QString err = m_emptyInput ? QString("Error parsing input file: the input contains no data") : checkForErrors(m_chdmanText);
	if (err.isEmpty() && st != QProcess::NormalExit)
		err = "CHDMAN crashed";
	else if (err.isEmpty() && exitCode != 0) {
		// Use chdman's own error line when there is one.
		for (const QString &l : m_chdmanText.split('\n'))
			if (l.startsWith("Error")) {
				err = l;
				break;
			}
		if (err.isEmpty())
			err = "CHDMAN exited with code " + QString::number(exitCode);
	}

	if (!err.isEmpty()) {
		if (!err.contains("file already exists") && !m_job.keepIncomplete && !m_job.toFileFull.isEmpty())
			deleteIncompleteOutput();
		QString rep = (err.contains("Error") ? QString() : QString("Error: ")) + err + "\n\n";
		rep += "Exit code: " + QString::number(exitCode) + "\n";
		if (!m_chdmanText.isEmpty())
			rep += "CHDMAN output:\n" + m_chdmanText;
		failAndFinish(err, rep, err + "  -  " + m_job.workingTitle);
		return;
	}

	// ---- success
	if (!m_tempDir.isEmpty()) {
		removeDirRecursively(m_tempDir);
		m_tempDir.clear();
	}

	if (!m_job.toFileFull.isEmpty() && !QFileInfo::exists(m_job.toFileFull)) {
		QString rep = "Error: CHDMAN finished without creating output file:\n\n" + m_job.toFileFull + "\n";
		rep += "Exit code: " + QString::number(exitCode) + "\n";
		if (!m_chdmanText.isEmpty())
			rep += "CHDMAN output:\n" + m_chdmanText;
		failAndFinish("Error: CHDMAN finished without creating output file", rep,
			"No output file created  -  " + m_job.workingTitle);
		return;
	}

	if (m_job.deleteInputFiles)
		deleteInputFiles();

	if (m_job.deleteInputDir && QFileInfo(m_job.workingDir).isDir()) {
		QString msg;
		if (!isDirEmpty(m_job.workingDir))
			msg = "Error deleting directory '" + m_job.workingDir + "' - Not empty";
		else
			msg = QDir().rmdir(m_job.workingDir) ? "Deleted directory '" + m_job.workingDir + "'"
			                                     : "Error deleting directory '" + m_job.workingDir + "'";
		log(msg);
		addReport(msg + "\n");
	}

	QString suffix = "completed";
	if (m_job.cmd == "verify") suffix = "verified";
	else if (m_job.cmd == "info") suffix = "read info";
	else if (m_job.cmd.startsWith("extract")) suffix = "extracted media";
	else if (m_job.cmd.startsWith("create")) suffix = "created";

	if (!m_job.toFileFull.isEmpty())
		m_finishSize = QFileInfo(m_job.toFileFull).size();

	if (m_job.cmd.startsWith("create")) {
		addReport("Successfully " + suffix + "\n\n");
		qint64 start = m_startSize;
		// For CUE/GDI/TOC the parser can undercount; trust chdman's logical size then.
		if (m_logicalSize > 0 && (start <= 0 || start < m_logicalSize / 10))
			start = m_logicalSize;
		if (start > 0) {
			const double pcnt = (1.0 - double(m_finishSize) / double(start)) * 100.0;
			addReport("Starting size: " + formatBytesReport(start) + " - Finished file size: " + formatBytesReport(m_finishSize)
				+ "\nTotal size saved: " + formatBytesReport(start - m_finishSize) + "  -  Space savings: "
				+ QString::number(pcnt, 'f', 2) + "%");
		} else {
			addReport("Starting size: Unknown - Finished file size: " + formatBytesReport(m_finishSize)
				+ "\nTotal size saved: Unknown  -  Space savings: Unknown");
		}
		m_startSize = start;
	} else if (m_job.cmd.startsWith("extract")) {
		addReport("Successfully " + suffix + "\n\nOutput folder: " + m_job.outputFolder + "\nOutput file:   " + m_job.toFile);
	} else if (m_job.cmd == "verify") {
		QString rep;
		for (const QString &l : m_chdmanText.split('\n')) {
			if (l.isEmpty() || l.contains("Compressed Hunks of Data"))
				continue;
			rep += l + "\n";
		}
		addReport(rep.isEmpty() ? "Successfully " + suffix + "\n\n" : rep);
	} else {
		addReport("Successfully " + suffix + "\n\n");
	}

	log("Successfully " + suffix + "  -  " + m_job.workingTitle);
	setProgress(100, "Successfully " + suffix + "  -  " + m_job.workingTitle);
	finish(JobStatus::Success);
}

// ------------------------------------------------------------------ cancel / cleanup

void JobRunner::cancel()
{
	if (m_done || m_cancelRequested)
		return;
	m_cancelRequested = true;
	log("Attempting to cancel " + m_job.workingTitle);
	setProgress(0, "Cancelling -  " + m_job.workingTitle);
	if (m_proc && m_proc->state() != QProcess::NotRunning) {
		m_proc->terminate();
		m_killTimer.start(3000);
		return;  // finishCancelled() runs from the finished handler
	}
	if (m_status != JobStatus::Pending)
		finishCancelled();
}

void JobRunner::finishCancelled()
{
	if (m_done)
		return;
	emit output("\nJob cancelled by user\n");
	addReport("Job cancelled by user\n\n");
	if (!m_job.keepIncomplete) {
		const QString managed = managedOutputSubdir();
		if (!managed.isEmpty()) {
			if (removeDirRecursively(managed)) {
				m_outputFolderCreated = false;
				log("Deleted created output folder '" + managed + "'");
				addReport("Deleted created output folder:\n    " + managed + "\n");
			} else {
				log("Error deleting created output folder '" + managed + "'");
				addReport("Error deleting created output folder '" + managed + "'\n");
			}
		} else if (!m_job.toFileFull.isEmpty() && QFileInfo::exists(m_job.toFileFull)) {
			deleteIncompleteOutput();
		}
	}
	if (!m_tempDir.isEmpty()) {
		removeDirRecursively(m_tempDir);
		m_tempDir.clear();
	}
	cleanupCreatedOutputFolder();
	log("Job " + QString::number(m_job.idx) + " cancelled by user");
	setProgress(100, "Cancelled -  " + m_job.workingTitle);
	finish(JobStatus::Cancelled);
}

void JobRunner::failAndFinish(const QString &logMsg, const QString &report, const QString &progressText)
{
	log(logMsg);
	addReport(report);
	if (!m_tempDir.isEmpty()) {
		removeDirRecursively(m_tempDir);
		m_tempDir.clear();
	}
	cleanupCreatedOutputFolder();
	setProgress(100, QString(progressText).remove('\n').remove('\r'));
	finish(JobStatus::Error);
}

void JobRunner::finish(JobStatus status)
{
	if (m_done)
		return;
	m_status = status;
	m_done = true;
	emit finished();
}

void JobRunner::deleteIncompleteOutput()
{
	if (!QFileInfo::exists(m_job.toFileFull))
		return;
	QStringList deleted;
	for (const QString &f : filesFromCueGdiToc(m_job.toFileFull))
		if (deleteFileWithRetry(f, 3, 100))
			deleted << f;
	const QString msg = deleted.isEmpty() ? "Error deleting incomplete file(s)!" : "Deleted incomplete file(s): " + deleted.join(", ");
	log(msg);
	addReport(deleted.isEmpty() ? msg + "\n" : formatReportList("Deleted incomplete file(s):", deleted));
}

void JobRunner::deleteInputFiles()
{
	QStringList ok, failed;
	for (const QString &f : filesFromCueGdiToc(m_job.fromFileFull)) {
		if (deleteFileWithRetry(f, 3, 25))
			ok << "'" + QFileInfo(f).fileName() + "'";
		else
			failed << "'" + QFileInfo(f).fileName() + "'";
	}
	if (ok.isEmpty() && failed.isEmpty())
		return;
	QString msg;
	if (!ok.isEmpty())
		msg = "Deleted Files: " + ok.join(", ");
	if (!failed.isEmpty())
		msg += (msg.isEmpty() ? "" : "\n") + QString("Error deleting: ") + failed.join(", ");
	log(msg);
	if (!ok.isEmpty())
		addReport(formatReportList("Deleted Files:", ok));
	if (!failed.isEmpty())
		addReport(formatReportList("Error deleting:", failed));
}

QString JobRunner::managedOutputSubdir() const
{
	// Only a per-job folder that this job created itself is ever removed wholesale.
	if (!m_job.outputFolderIsPerJob || m_job.outputFolder.isEmpty() || m_job.toFileNoExt.isEmpty())
		return {};
	if (!m_outputFolderCreated || !QFileInfo(m_job.outputFolder).isDir())
		return {};
	const QString folder = QDir::cleanPath(m_job.outputFolder);
	if (QFileInfo(folder).fileName() != m_job.toFileNoExt)
		return {};
	if (folder == QDir::cleanPath(m_job.workingDir))
		return {};
	if (m_job.fromFileFull.startsWith(folder + "/"))
		return {};
	return folder;
}

void JobRunner::cleanupCreatedOutputFolder()
{
	if (m_job.keepIncomplete)
		return;
	const QString folder = managedOutputSubdir();
	if (folder.isEmpty())
		return;
	if (removeDirRecursively(folder)) {
		m_outputFolderCreated = false;
		emit output("Deleted created output directory '" + folder + "'\n");
	}
}

} // namespace namdhc
