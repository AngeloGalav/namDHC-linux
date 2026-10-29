// SPDX-License-Identifier: GPL-3.0-or-later
#include "jobqueue.h"

#include "utils.h"

#include <QTimer>
#include <QVariant>
#include <cmath>

namespace namdhc {

JobQueue::JobQueue(const RunnerConfig &cfg, QObject *parent)
	: QObject(parent), m_cfg(cfg)
{
}

JobQueue::~JobQueue()
{
	for (JobRunner *r : m_slots)
		delete r;
}

void JobQueue::start(const QList<Job> &jobs, int concurrency)
{
	for (JobRunner *r : m_slots)
		delete r;
	m_slots.clear();
	m_pending = jobs;
	m_tally = Tally();
	m_tally.total = jobs.size();
	m_allReport.clear();
	m_cancelAll = false;
	m_running = true;
	m_elapsedMs = -1;
	m_timer.start();
	m_slots.resize(qBound(1, qMin(concurrency, int(jobs.size())), 64));
	m_slots.fill(nullptr);

	emit logMessage(QString::number(m_tally.total) + " " + (jobs.isEmpty() ? QString() : jobs.first().cmd.toUpper()) + " jobs starting ...");
	emit overallProgress(0, overallText());
	if (jobs.isEmpty()) {
		m_running = false;
		m_elapsedMs = 0;
		emit allFinished();
		return;
	}
	// Defer so callers can finish wiring up signals/UI first.
	QTimer::singleShot(0, this, &JobQueue::fillSlots);
}

const JobRunner *JobQueue::slotRunner(int slot) const
{
	return slot >= 0 && slot < m_slots.size() ? m_slots[slot] : nullptr;
}

void JobQueue::fillSlots()
{
	if (m_filling || !m_running)
		return;
	m_filling = true;
	for (int slot = 0; slot < m_slots.size(); ++slot) {
		if (m_pending.isEmpty())
			break;
		// A slot is free only once its previous runner has been counted.
		if (m_slots[slot] && !m_slots[slot]->property("counted").toBool())
			continue;
		if (m_slots[slot]) {
			m_slots[slot]->deleteLater();
			m_slots[slot] = nullptr;
		}
		const Job job = m_pending.takeFirst();
		auto *runner = new JobRunner(job, m_cfg, this);
		m_slots[slot] = runner;
		++m_tally.started;
		connect(runner, &JobRunner::progress, this, [this, slot](int pct, const QString &text, bool busy) {
			emit slotProgress(slot, pct, text, busy);
		});
		connect(runner, &JobRunner::output, this, [this, slot](const QString &text) { emit slotOutput(slot, text); });
		connect(runner, &JobRunner::logMessage, this, [this, runner](const QString &msg) {
			emit logMessage("Job " + QString::number(runner->job().idx) + " - " + msg);
		});
		connect(runner, &JobRunner::finished, this, [this, slot] {
			// Finish handling may start new runners; never do that re-entrantly.
			QTimer::singleShot(0, this, [this, slot] { onRunnerFinished(slot); });
		});
		emit slotStarted(slot, job);
		runner->start();
	}
	m_filling = false;
}

void JobQueue::onRunnerFinished(int slot)
{
	JobRunner *r = m_slots.value(slot);
	if (!r || !r->isDone() || r->property("counted").toBool())
		return;
	r->setProperty("counted", true);

	const Job &job = r->job();
	const JobStatus st = r->status();
	switch (st) {
	case JobStatus::Success:
		++m_tally.success;
		emit statusMessage("Job " + QString::number(job.idx) + " finished successfully!");
		break;
	case JobStatus::FileExists:
		++m_tally.skipped;
		emit statusMessage("Job " + QString::number(job.idx) + " skipped");
		break;
	case JobStatus::Cancelled:
		++m_tally.cancelled;
		emit statusMessage("Job " + QString::number(job.idx) + " cancelled");
		break;
	default:
		++m_tally.withError;
		emit statusMessage("Job " + QString::number(job.idx) + " failed");
		break;
	}
	m_tally.totalFileStartSize += r->fileStartSize() > 0 && st == JobStatus::Success ? r->fileStartSize() : 0;
	m_tally.totalFileFinishSize += st == JobStatus::Success ? r->fileFinishSize() : 0;

	emit jobFinished(slot, job, st);
	markFinished(r->report());

	if (m_tally.finished >= m_tally.total) {
		m_running = false;
		m_elapsedMs = m_timer.elapsed();
		emit allFinished();
		return;
	}
	fillSlots();
}

void JobQueue::markFinished(const QString &reportText)
{
	m_allReport += reportText;
	++m_tally.finished;
	emit overallProgress(overallPercent(), overallText());
}

int JobQueue::overallPercent() const
{
	if (m_tally.total <= 0)
		return 0;
	return qMin(100, int(std::ceil(100.0 * m_tally.finished / m_tally.total)));
}

QString JobQueue::overallText() const
{
	QString t = QString::number(m_tally.finished) + " jobs of " + QString::number(m_tally.total) + " completed ";
	if (m_tally.withError)
		t += "(" + QString::number(m_tally.withError) + " error" + (m_tally.withError > 1 ? "s)" : ")");
	else
		t += " - " + QString::number(overallPercent()) + "%";
	return t;
}

void JobQueue::cancelSlot(int slot)
{
	if (JobRunner *r = m_slots.value(slot))
		r->cancel();
}

void JobQueue::cancelAll()
{
	if (!m_running || m_cancelAll)
		return;
	m_cancelAll = true;
	const QList<Job> pending = m_pending;
	m_pending.clear();
	for (const Job &job : pending) {
		++m_tally.cancelled;
		markFinished("\n\n" + job.cmd.toUpper() + " - " + job.workingTitle + "\n" + drawLine(77) + "\nJob cancelled by user\n");
	}
	emit statusMessage("Cancelling running jobs...");
	bool anyRunning = false;
	for (JobRunner *r : m_slots) {
		if (r && !r->isDone()) {
			anyRunning = true;
			r->cancel();
		}
	}
	if (!anyRunning && m_tally.finished >= m_tally.total && m_running) {
		m_running = false;
		m_elapsedMs = m_timer.elapsed();
		emit allFinished();
	}
}

QString JobQueue::summaryText(const QString &finishedText) const
{
	QString s = "Total number of jobs attempted: " + QString::number(m_tally.total) + "\n";
	if (m_tally.success)
		s += finishedText + " successfully: " + QString::number(m_tally.success) + "\n";
	if (m_tally.cancelled)
		s += "Jobs cancelled by the user: " + QString::number(m_tally.cancelled) + "\n";
	if (m_tally.skipped)
		s += "Jobs skipped because the output file already exists: " + QString::number(m_tally.skipped) + "\n";
	if (m_tally.withError)
		s += "Jobs that finished with errors: " + QString::number(m_tally.withError) + "\n";
	s += "Total time to finish: " + millisecToTime(elapsedMs());
	return s;
}

QString JobQueue::sizeSavingsText() const
{
	const qint64 a = m_tally.totalFileStartSize, b = m_tally.totalFileFinishSize;
	if (a <= 0 || b <= 0)
		return {};
	return "Total size before compression: " + formatBytesReport(a) + "\nTotal size after compression: " + formatBytesReport(b)
		+ "\nSpace savings: " + formatBytesReport(a - b) + " (" + QString::number(qRound((1.0 - double(b) / double(a)) * 100.0)) + "%)";
}

} // namespace namdhc
