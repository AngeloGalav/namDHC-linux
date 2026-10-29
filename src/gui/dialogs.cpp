// SPDX-License-Identifier: GPL-3.0-or-later
#include "dialogs.h"

#include "settings.h"
#include "utils.h"
#include "version.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace namdhc {

static const char *VersionHistory = R"(v2.0.0 (Linux)
- Native Linux port of namDHC v2.0 written in C++/Qt6
- Bundles a native build of MAME's chdman
- New namdhc command line tool: batch conversion of whole folders (namdhc --folder .),
  automatic CD/DVD detection, live progress for concurrent jobs, dry runs and reports
- 7-Zip (7z/7zz) and unzip support for archived input
- Drag and drop of files and folders onto the file list


History of the original Windows version (namDHC by Umageddon)

v2.0
- Migrated to AutoHotkey v2
- Fixes for GUI/threading/message handling, queue/cancel robustness and job execution stability
- Added 7-Zip support
- Added drag and drop support for input files. Drop folders or files on the main file list window to add
- Removed CHDMAN version checking
- Updated bundled CHDMAN to 0.287
- Added DVD create/extract support
- Removed batch updating - now we just go to github
- Other fixes and tons of changes

v1.13
- Final report shows size savings when compressing to CHD
- Fixed output folder being cleared if cancel was clicked in the select folder helper
- Removed close button on console windows
- Some cleanup

v1.12
- Added an option to force no DPI scaling for people who are having issues with options being cut off in window (Thanks TFWol)
- Fixed quit routine

v1.11
- Possible crash fixes (?)
- Fixed zip files not being cleared from list after a successful job
- Slight speed improvement and (hopefully) improved reliability when cancelling jobs

v1.10
- Possible fix crashing and slowdown on some machines (Removed Autohotkey speedups :P)
- Fixed namDHC sometimes deleting the output file if it already existed
- Fixed a possible chdman timeout issue
- Fixed reporting to not show old reports from previous jobs

v1.09
- Fixed select output folder button not launching explorer window
- Fixed sometimes showing multiple jobs in one progress slot
- Re-enabled Autohotkey 'speedups'
- Fixed line character being misrepresented in report

v1.08
- Fixed Verify option
- No need to hit enter to confirm a new output folder
- Limit output folder name to 255 characters
- Removed Add/Remove metadata indefinitely

v1.07
- Added zip file support
- Changed quit routine
- Changed error handling for output chd files that already exist
- Bug fixes

v1.06
- Allows creation of multiple formats during a single job
- User will be prompted to rename any duplicate output files
- Changed the finished job sound to play media instead of just beeps

v1.05
- Added update functionality
- Bug fixes

v1.03
- Fixed Cancel all jobs button
- Added time elapsed to report

v1.01
- Added ISO input media support

v1.00
- Initial release
)";

// ------------------------------------------------------------------ VerboseWindow

VerboseWindow::VerboseWindow(QWidget *parent)
	: QWidget(parent, Qt::Window)
{
	setWindowTitle(QStringLiteral(NAMDHC_APP_NAME " - Verbose"));
	auto *lay = new QVBoxLayout(this);
	lay->setContentsMargins(5, 5, 5, 5);
	m_text = new QPlainTextEdit(this);
	m_text->setReadOnly(true);
	m_text->setMaximumBlockCount(20000);
	m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	lay->addWidget(m_text);
	resize(800, 400);
	restoreGeometry(appSettings().value(keys::VerboseWindowGeometry).toByteArray());
}

void VerboseWindow::log(const QString &msg)
{
	if (msg.isEmpty())
		return;
	m_text->appendPlainText("[" + QTime::currentTime().toString("HH:mm:ss") + "]  " + msg);
	m_text->verticalScrollBar()->setValue(m_text->verticalScrollBar()->maximum());
}

void VerboseWindow::closeEvent(QCloseEvent *e)
{
	saveGeometrySetting();
	e->accept();
	emit hiddenByUser();
}

void VerboseWindow::moveEvent(QMoveEvent *e)
{
	QWidget::moveEvent(e);
	if (isVisible())
		saveGeometrySetting();
}

void VerboseWindow::resizeEvent(QResizeEvent *e)
{
	QWidget::resizeEvent(e);
	if (isVisible())
		saveGeometrySetting();
}

void VerboseWindow::saveGeometrySetting()
{
	appSettings().setValue(keys::VerboseWindowGeometry, saveGeometry());
}

// ------------------------------------------------------------------ JobConsole

JobConsole::JobConsole(const QString &title, QWidget *parent)
	: QWidget(parent, Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowMinMaxButtonsHint)
{
	setWindowTitle(title);
	setAttribute(Qt::WA_DeleteOnClose);
	setAttribute(Qt::WA_ShowWithoutActivating);
	auto *lay = new QVBoxLayout(this);
	lay->setContentsMargins(0, 0, 0, 0);
	m_text = new QPlainTextEdit(this);
	m_text->setReadOnly(true);
	m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	m_text->setStyleSheet("QPlainTextEdit { background: #0c0c0c; color: #cccccc; }");
	lay->addWidget(m_text);
	resize(720, 360);
}

void JobConsole::append(const QString &text)
{
	// Emulate a terminal: '\r' rewrites the current line (chdman progress).
	QTextCursor c = m_text->textCursor();
	c.movePosition(QTextCursor::End);
	for (const QChar ch : text) {
		if (ch == '\r') {
			m_lastWasCarriageReturn = true;
			continue;
		}
		if (m_lastWasCarriageReturn && ch != '\n') {
			c.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
			c.removeSelectedText();
		}
		m_lastWasCarriageReturn = false;
		c.insertText(QString(ch));
	}
	m_text->setTextCursor(c);
	m_text->ensureCursorVisible();
}

// ------------------------------------------------------------------ ChdInfoDialog

ChdInfoDialog::ChdInfoDialog(const QString &chdman, const QStringList &files, QWidget *parent)
	: QDialog(parent), m_chdman(chdman), m_files(files)
{
	setWindowTitle("CHD Info");
	setAttribute(Qt::WA_DeleteOnClose);
	auto *lay = new QVBoxLayout(this);
	lay->setContentsMargins(20, 20, 20, 20);

	m_title = new QLabel(this);
	QFont tf = m_title->font();
	tf.setPointSizeF(tf.pointSizeF() * 1.2);
	tf.setBold(true);
	m_title->setFont(tf);
	m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
	lay->addWidget(m_title);

	m_form = new QFormLayout;
	m_form->setLabelAlignment(Qt::AlignLeft);
	lay->addLayout(m_form);

	auto *compLabel = new QLabel("<b>Compression</b>", this);
	lay->addSpacing(10);
	lay->addWidget(compLabel);
	m_compression = new QTableWidget(0, 3, this);
	m_compression->setHorizontalHeaderLabels({"Hunks", "Type", "Percent"});
	m_compression->verticalHeader()->hide();
	m_compression->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	m_compression->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_compression->setSelectionMode(QAbstractItemView::NoSelection);
	lay->addWidget(m_compression);

	lay->addSpacing(10);
	lay->addWidget(new QLabel("<b>Metadata</b>", this));
	m_metadata = new QPlainTextEdit(this);
	m_metadata->setReadOnly(true);
	m_metadata->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	m_metadata->setMinimumHeight(180);
	lay->addWidget(m_metadata, 1);

	m_loading = new QLabel("Loading...", this);
	m_loading->setAlignment(Qt::AlignCenter);
	m_loading->setFrameShape(QFrame::Box);
	m_loading->hide();
	lay->addWidget(m_loading);

	auto *nav = new QHBoxLayout;
	m_prev = new QPushButton("< Prev file", this);
	m_prev->setToolTip("Show the previous CHD in the info list");
	m_combo = new QComboBox(this);
	m_combo->setToolTip("Choose which CHD to show in the info window");
	for (const QString &f : m_files)
		m_combo->addItem(QFileInfo(f).fileName(), f);
	m_next = new QPushButton("Next file >", this);
	m_next->setToolTip("Show the next CHD in the info list");
	nav->addWidget(m_prev);
	nav->addWidget(m_combo, 1);
	nav->addWidget(m_next);
	lay->addLayout(nav);

	connect(m_prev, &QPushButton::clicked, this, [this] { showFile(m_index - 1); });
	connect(m_next, &QPushButton::clicked, this, [this] { showFile(m_index + 1); });
	connect(m_combo, &QComboBox::activated, this, [this](int i) { showFile(i); });

	resize(820, 760);
	showFile(0);
}

void ChdInfoDialog::setLoading(bool loading)
{
	m_loading->setVisible(loading);
	m_prev->setEnabled(!loading && m_index > 0);
	m_next->setEnabled(!loading && m_index < m_files.size() - 1);
	m_combo->setEnabled(!loading);
}

void ChdInfoDialog::showFile(int index)
{
	if (m_files.isEmpty() || (m_proc && m_proc->state() != QProcess::NotRunning))
		return;
	m_index = qBound(0, index, int(m_files.size()) - 1);
	m_combo->setCurrentIndex(m_index);
	const QString file = m_files[m_index];
	m_title->setText(QString("[%1/%2]  %3").arg(m_index + 1).arg(m_files.size()).arg(QFileInfo(file).fileName()));

	// Only show "Loading..." if reading takes a while.
	QTimer::singleShot(500, this, [this] {
		if (m_proc && m_proc->state() != QProcess::NotRunning)
			setLoading(true);
	});
	m_prev->setEnabled(false);
	m_next->setEnabled(false);

	m_proc = new QProcess(this);
	m_proc->setProcessChannelMode(QProcess::MergedChannels);
	m_proc->setWorkingDirectory(QFileInfo(file).absolutePath());
	connect(m_proc, &QProcess::finished, this, &ChdInfoDialog::onFinished);
	connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart)
			onFinished();
	});
	m_proc->start(m_chdman, {"info", "-v", "-i", file});
}

void ChdInfoDialog::onFinished()
{
	const QString text = QString::fromUtf8(m_proc->readAll());
	m_proc->deleteLater();
	m_proc = nullptr;
	setLoading(false);

	while (m_form->rowCount() > 0)
		m_form->removeRow(0);
	m_compression->setRowCount(0);

	const ChdInfo info = parseChdInfo(text);
	if (info.fields.isEmpty()) {
		m_metadata->setPlainText(text.trimmed().isEmpty() ? "Unable to read CHD information" : text);
		return;
	}
	for (const auto &kv : info.fields) {
		auto *edit = new QLineEdit(kv.second, this);
		edit->setReadOnly(true);
		auto *label = new QLabel("<b>" + kv.first.toHtmlEscaped() + ":</b>", this);
		m_form->addRow(label, edit);
	}
	for (const auto &row : info.compression) {
		const int r = m_compression->rowCount();
		m_compression->insertRow(r);
		m_compression->setItem(r, 0, new QTableWidgetItem(row.hunks));
		m_compression->setItem(r, 1, new QTableWidgetItem(row.name));
		m_compression->setItem(r, 2, new QTableWidgetItem(row.percent));
	}
	m_compression->resizeColumnToContents(0);
	m_compression->resizeColumnToContents(2);
	int h = m_compression->horizontalHeader()->height() + 2 * m_compression->frameWidth();
	for (int r = 0; r < m_compression->rowCount(); ++r)
		h += m_compression->rowHeight(r);
	m_compression->setFixedHeight(qMax(h, 60));
	m_metadata->setPlainText(info.metadata);
	m_combo->setFocus();
}

// ------------------------------------------------------------------ ReportDialog

ReportDialog::ReportDialog(const QString &title, const QString &summary, const QString &savings, const QString &report, QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle("REPORT");
	setAttribute(Qt::WA_DeleteOnClose);
	auto *lay = new QVBoxLayout(this);
	auto *t = new QLabel("<b><big>" + title.toHtmlEscaped() + "</big></b>", this);
	lay->addWidget(t);
	auto *s = new QLabel(summary, this);
	QFont bf = s->font();
	bf.setBold(true);
	s->setFont(bf);
	s->setTextInteractionFlags(Qt::TextSelectableByMouse);
	lay->addWidget(s);
	if (!savings.isEmpty()) {
		auto *sv = new QLabel(savings, this);
		sv->setFont(bf);
		sv->setTextInteractionFlags(Qt::TextSelectableByMouse);
		lay->addSpacing(8);
		lay->addWidget(sv);
	}
	auto *text = new QPlainTextEdit(report.trimmed(), this);
	text->setReadOnly(true);
	text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	lay->addSpacing(12);
	lay->addWidget(text, 1);
	auto *box = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
	lay->addWidget(box);
	resize(860, 640);
}

// ------------------------------------------------------------------ AboutDialog

AboutDialog::AboutDialog(QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle("About " NAMDHC_APP_NAME);
	setAttribute(Qt::WA_DeleteOnClose);
	auto *lay = new QVBoxLayout(this);
	lay->setContentsMargins(20, 20, 20, 20);

	auto *head = new QLabel(QString("<span style='font-size:20pt; font-weight:700'>%1</span>"
	                                "<span style='font-size:13pt; font-weight:700'>&nbsp;v%2</span>")
	                            .arg(NAMDHC_APP_NAME, NAMDHC_VERSION),
		this);
	lay->addWidget(head);
	lay->addWidget(new QLabel("A Linux frontend for the MAME CHDMAN tool", this));

	lay->addSpacing(12);
	lay->addWidget(new QLabel("History", this));
	auto *hist = new QPlainTextEdit(VersionHistory, this);
	hist->setReadOnly(true);
	hist->setMinimumHeight(220);
	lay->addWidget(hist, 1);

	auto link = [this](const QString &label, const QString &url) {
		auto *l = new QLabel(label + ": <a href=\"" + url + "\">" + url + "</a>", this);
		l->setOpenExternalLinks(true);
		return l;
	};
	const QString repo = QStringLiteral(NAMDHC_GITHUB_REPO);
	if (!repo.isEmpty())
		lay->addWidget(link("Github", "https://github.com/" + repo));
	lay->addWidget(link("Original namDHC", "https://github.com/umageddon/namDHC"));
	lay->addWidget(link("MAME Info", "https://www.mamedev.org/"));
	auto *copy = new QLabel("namDHC (C) Copyright 2026 Umageddon  -  Linux port released under the GPL v3", this);
	QFont sf = copy->font();
	sf.setPointSizeF(sf.pointSizeF() * 0.9);
	copy->setFont(sf);
	lay->addSpacing(8);
	lay->addWidget(copy);
	resize(800, 560);
}

// ------------------------------------------------------------------ helpers

DuplicateAction askDuplicate(QWidget *parent, const Job &newJob, const Job &existing, int number)
{
	QMessageBox box(QMessageBox::Question, "Duplicate filename",
		"A duplicate conversion was found.\n\nThe file \"" + newJob.fromFileFull + "\" would create a " + newJob.toFileExt.toUpper()
			+ " file that has the same name as another file already in the job queue.\n\nSelect [Rename] to rename the output file \""
			+ newJob.toFile + "\" to \"" + existing.toFileNoExt + " [#" + QString::number(number) + "]." + newJob.toFileExt
			+ "\"\n\nSelect [Skip] to skip this job",
		QMessageBox::NoButton, parent);
	QPushButton *rename = box.addButton("&Rename", QMessageBox::YesRole);
	QPushButton *skip = box.addButton("&Skip", QMessageBox::NoRole);
	QPushButton *all = box.addButton("Rename &All", QMessageBox::AcceptRole);
	box.setDefaultButton(rename);
	box.exec();
	if (box.clickedButton() == skip)
		return DuplicateAction::Skip;
	if (box.clickedButton() == all)
		return DuplicateAction::RenameAll;
	return DuplicateAction::Rename;
}

bool confirmAutoClose(QWidget *parent, const QString &title, const QString &text, const std::function<bool()> &expired)
{
	QMessageBox box(QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::No, parent);
	box.setDefaultButton(QMessageBox::No);
	QTimer timer;
	QObject::connect(&timer, &QTimer::timeout, &box, [&] {
		if (expired && expired())
			box.done(QMessageBox::No);
	});
	timer.start(250);
	return box.exec() == QMessageBox::Yes;
}

void playFinishedSound()
{
	if (const QString p = QStandardPaths::findExecutable("canberra-gtk-play"); !p.isEmpty()) {
		QProcess::startDetached(p, {"-i", "complete", "-d", "namDHC"});
		return;
	}
	const QString oga = "/usr/share/sounds/freedesktop/stereo/complete.oga";
	for (const QString &player : {QString("pw-play"), QString("paplay")}) {
		const QString p = QStandardPaths::findExecutable(player);
		if (!p.isEmpty() && QFileInfo::exists(oga)) {
			QProcess::startDetached(p, {oga});
			return;
		}
	}
	QApplication::beep();
}

} // namespace namdhc
