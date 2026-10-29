// SPDX-License-Identifier: GPL-3.0-or-later
// Runs a list of jobs with a fixed number of concurrent slots.
#pragma once

#include "jobrunner.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QVector>

namespace namdhc {

struct Tally {
	int total = 0;
	int started = 0;
	int success = 0;
	int cancelled = 0;
	int skipped = 0;
	int withError = 0;
	int finished = 0;
	qint64 totalFileStartSize = 0;
	qint64 totalFileFinishSize = 0;
};

class JobQueue : public QObject {
	Q_OBJECT
public:
	explicit JobQueue(const RunnerConfig &cfg, QObject *parent = nullptr);
	~JobQueue() override;

	void start(const QList<Job> &jobs, int concurrency);
	void cancelSlot(int slot);
	void cancelAll();

	bool isRunning() const { return m_running; }
	int slotCount() const { return m_slots.size(); }
	const JobRunner *slotRunner(int slot) const;
	const Tally &tally() const { return m_tally; }
	QString allReport() const { return m_allReport; }
	qint64 elapsedMs() const { return m_elapsedMs >= 0 ? m_elapsedMs : m_timer.elapsed(); }

	// Plain-text summary shown in the final report ("Total number of jobs attempted: ...").
	QString summaryText(const QString &finishedText) const;
	// "Total size before compression: ..." block for create jobs, empty if unknown.
	QString sizeSavingsText() const;
	QString overallText() const;
	int overallPercent() const;

signals:
	void slotStarted(int slot, const namdhc::Job &job);
	void slotProgress(int slot, int percent, const QString &text, bool busy);
	void slotOutput(int slot, const QString &text);
	void jobFinished(int slot, const namdhc::Job &job, namdhc::JobStatus status);
	void overallProgress(int percent, const QString &text);
	void logMessage(const QString &msg);
	void statusMessage(const QString &msg);
	void allFinished();

private:
	void fillSlots();
	void onRunnerFinished(int slot);
	void markFinished(const QString &reportText);

	RunnerConfig m_cfg;
	QList<Job> m_pending;
	QVector<JobRunner *> m_slots;
	Tally m_tally;
	QString m_allReport;
	QElapsedTimer m_timer;
	qint64 m_elapsedMs = -1;
	bool m_running = false;
	bool m_cancelAll = false;
	bool m_filling = false;
};

} // namespace namdhc
