// SPDX-License-Identifier: GPL-3.0-or-later
// Secondary windows of the GUI: verbose log, job consoles, CHD info, report, about.
#pragma once

#include "chdinfo.h"
#include "job.h"

#include <QDialog>
#include <QPointer>
#include <QProcess>
#include <QWidget>

#include <functional>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QFormLayout;

namespace namdhc {

// "namDHC - Verbose" log window.
class VerboseWindow : public QWidget {
	Q_OBJECT
public:
	explicit VerboseWindow(QWidget *parent = nullptr);
	void log(const QString &msg);

protected:
	void closeEvent(QCloseEvent *e) override;
	void moveEvent(QMoveEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

signals:
	void hiddenByUser();

private:
	void saveGeometrySetting();
	QPlainTextEdit *m_text;
};

// Live chdman output of one job ("Show a chdman console for each new job").
class JobConsole : public QWidget {
	Q_OBJECT
public:
	explicit JobConsole(const QString &title, QWidget *parent = nullptr);
	void append(const QString &text);

private:
	QPlainTextEdit *m_text;
	bool m_lastWasCarriageReturn = false;
};

// "CHD Info" window with previous/next navigation.
class ChdInfoDialog : public QDialog {
	Q_OBJECT
public:
	ChdInfoDialog(const QString &chdman, const QStringList &files, QWidget *parent = nullptr);

private:
	void showFile(int index);
	void onFinished();
	void setLoading(bool loading);

	QString m_chdman;
	QStringList m_files;
	int m_index = 0;
	QProcess *m_proc = nullptr;

	QLabel *m_title;
	QLabel *m_loading;
	QFormLayout *m_form;
	QTableWidget *m_compression;
	QPlainTextEdit *m_metadata;
	QPushButton *m_prev;
	QPushButton *m_next;
	QComboBox *m_combo;
};

// Final job report.
class ReportDialog : public QDialog {
	Q_OBJECT
public:
	ReportDialog(const QString &title, const QString &summary, const QString &savings, const QString &report, QWidget *parent = nullptr);
};

class AboutDialog : public QDialog {
	Q_OBJECT
public:
	explicit AboutDialog(QWidget *parent = nullptr);

signals:
	void checkForUpdatesRequested();
};

// Duplicate output name prompt: Rename / Skip / Rename All.
DuplicateAction askDuplicate(QWidget *parent, const Job &newJob, const Job &existing, int number);

// Yes/No question that closes itself (answering "No") once `expired` returns true,
// e.g. when the job the user wants to cancel has finished meanwhile.
bool confirmAutoClose(QWidget *parent, const QString &title, const QString &text, const std::function<bool()> &expired);

void playFinishedSound();

} // namespace namdhc
