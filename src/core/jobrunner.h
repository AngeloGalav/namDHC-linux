// SPDX-License-Identifier: GPL-3.0-or-later
// Runs one job: optional archive extraction, chdman, cleanup and reporting.
// This is the Linux counterpart of namDHC's threads.ahk worker.
#pragma once

#include "job.h"

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QTimer>

namespace namdhc {

enum class JobStatus { Pending, Starting, Unzipping, Running, Success, Error, FileExists, Cancelled };

QString jobStatusName(JobStatus s);

struct RunnerConfig {
	QString chdman;
	QString sevenZip;   // may be empty
	QString tempDir;    // scratch folder for archive extraction
};

class JobRunner : public QObject {
	Q_OBJECT
public:
	JobRunner(const Job &job, const RunnerConfig &cfg, QObject *parent = nullptr);
	~JobRunner() override;

	void start();
	void cancel();

	const Job &job() const { return m_job; }
	JobStatus status() const { return m_status; }
	bool isDone() const { return m_done; }
	bool cancelRequested() const { return m_cancelRequested; }
	qint64 fileStartSize() const { return m_startSize; }
	qint64 fileFinishSize() const { return m_finishSize; }
	QString report() const;            // full per-job report with header
	QString commandLine() const { return m_commandLine; }
	int progressPercent() const { return m_percent; }
	QString resultMessage() const { return m_lastLog; }  // last log line, e.g. the error

signals:
	void progress(int percent, const QString &text, bool busy);
	void logMessage(const QString &msg);
	void output(const QString &text);  // raw chdman / archiver output
	void finished();

private:
	void setProgress(int percent, const QString &text, bool busy = false);
	void addReport(const QString &text);
	void log(const QString &msg);

	void startArchiveExtraction();
	void onArchiveOutput();
	void onArchiveFinished(int exitCode, QProcess::ExitStatus st);
	void startChdman();
	void onChdmanOutput();
	void onChdmanFinished(int exitCode, QProcess::ExitStatus st);
	void parseChdmanSegment(const QString &segment);

	void failAndFinish(const QString &logMsg, const QString &report, const QString &progressText);
	void finishCancelled();
	void finish(JobStatus status);

	void deleteIncompleteOutput();
	void deleteInputFiles();
	void cleanupCreatedOutputFolder();
	QString managedOutputSubdir() const;
	QString findExtractedInput() const;
	QString checkForErrors(const QString &text) const;

	Job m_job;
	RunnerConfig m_cfg;
	JobStatus m_status = JobStatus::Pending;
	bool m_done = false;
	bool m_cancelRequested = false;
	bool m_outputFolderCreated = false;
	bool m_emptyInput = false;

	QProcess *m_proc = nullptr;
	QTimer m_killTimer;
	QString m_tempDir;
	QString m_inputFile;          // what chdman actually reads (may be inside m_tempDir)
	QString m_partial;            // incomplete output segment
	QString m_chdmanText;         // non-progress chdman lines
	QString m_commandLine;
	QString m_report;
	QString m_lastLog;
	int m_percent = 0;
	qint64 m_startSize = 0;
	qint64 m_finishSize = 0;
	qint64 m_logicalSize = 0;
};

} // namespace namdhc
