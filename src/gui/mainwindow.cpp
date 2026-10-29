// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"

#include "dialogs.h"
#include "settings.h"
#include "utils.h"
#include "version.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

namespace namdhc {

namespace {

constexpr int OptionsPerColumn = 9;

QString yesNo(bool b)
{
	return b ? "yes" : "no";
}

bool settingYes(const char *key, bool def)
{
	return appSettings().value(key, yesNo(def)).toString() == "yes";
}

QString nearestExistingDir(const QString &dir, const QString &fallback)
{
	QString d = dir;
	while (!d.isEmpty() && !QFileInfo(d).isDir()) {
		const QString parent = QFileInfo(d).absolutePath();
		if (parent == d)
			break;
		d = parent;
	}
	return QFileInfo(d).isDir() ? d : fallback;
}

QString defaultBrowseDir()
{
	const QString desktop = QDir::homePath() + "/Desktop";
	return QFileInfo(desktop).isDir() ? desktop : QDir::homePath();
}

} // namespace

MainWindow::MainWindow(const QString &chdman, QWidget *parent)
	: QMainWindow(parent), m_chdman(chdman)
{
	setWindowTitle(NAMDHC_APP_NAME);
	setWindowIcon(QIcon(":/namdhc.svg"));
	setAcceptDrops(true);

	m_outputFolder = appSettings().value(keys::OutputFolder, QDir::homePath()).toString();

	m_verbose = new VerboseWindow(nullptr);
	connect(m_verbose, &VerboseWindow::hiddenByUser, this, [this] {
		m_actVerbose->setChecked(false);
		appSettings().setValue(keys::ShowVerboseWindow, "no");
	});

	buildUi();
	buildMenus();
	restoreGeometry(appSettings().value(keys::MainWindowGeometry).toByteArray());

	showVerbose(m_actVerbose->isChecked());
	selectJob();

	log(QString(NAMDHC_APP_NAME) + " ready.");
	log("Using chdman: " + m_chdman);
	const QString sz = sevenZip();
	log(sz.isEmpty() ? QString("7-Zip not found: .7z input disabled") : "Using 7-Zip: " + sz);
}

MainWindow::~MainWindow()
{
	delete m_verbose;
}

// ------------------------------------------------------------------ UI construction

void MainWindow::buildUi()
{
	auto *central = new QWidget(this);
	auto *main = new QVBoxLayout(central);
	main->setContentsMargins(8, 8, 8, 8);
	setCentralWidget(central);

	// ---- Jobs
	m_jobsGroup = new QGroupBox("Jobs", central);
	auto *jl = new QVBoxLayout(m_jobsGroup);

	auto *row1 = new QHBoxLayout;
	row1->addWidget(new QLabel("Job type:"));
	m_jobCombo = new QComboBox;
	for (JobType t : {JobType::Create, JobType::Extract, JobType::Info, JobType::Verify})
		m_jobCombo->addItem(jobTypeLabel(t), int(t));
	m_jobCombo->setMinimumWidth(220);
	m_jobCombo->setToolTip("Choose whether to create, extract, verify, or read CHD info");
	row1->addWidget(m_jobCombo);
	row1->addSpacing(30);
	row1->addWidget(new QLabel("Media type:"));
	m_mediaCombo = new QComboBox;
	m_mediaCombo->setMinimumWidth(220);
	m_mediaCombo->setToolTip("Choose the media type for the current job");
	row1->addWidget(m_mediaCombo);
	row1->addStretch();
	jl->addLayout(row1);

	jl->addSpacing(6);
	jl->addWidget(new QLabel("Input files"));
	auto *row2 = new QHBoxLayout;
	m_addFilesBtn = new QPushButton("Add files");
	m_addFilesBtn->setToolTip("Add one or more files to the job list");
	m_addFolderBtn = new QPushButton("Add a folder");
	m_addFolderBtn->setToolTip("Scan a folder and add matching files to the job list");
	m_inputExtBtn = new QPushButton("Select input file types");
	m_inputExtMenu = new QMenu(m_inputExtBtn);
	m_inputExtBtn->setMenu(m_inputExtMenu);
	m_inputExtBtn->setMinimumWidth(233);
	row2->addWidget(m_addFilesBtn);
	row2->addWidget(m_addFolderBtn);
	row2->addStretch();
	row2->addWidget(m_inputExtBtn);
	jl->addLayout(row2);

	m_list = new QListWidget;
	m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_list->setMinimumHeight(153);
	m_list->setToolTip("Drop files or folders here to add them");
	jl->addWidget(m_list, 1);

	auto *row3 = new QHBoxLayout;
	m_selectAllBtn = new QPushButton("Select all");
	m_selectAllBtn->setToolTip("Select every file in the current list");
	m_unselectBtn = new QPushButton("Unselect");
	m_unselectBtn->setToolTip("Clear the current file selection");
	m_removeBtn = new QPushButton("Remove selection");
	m_removeBtn->setToolTip("Remove the selected entries from the file list");
	row3->addWidget(m_selectAllBtn);
	row3->addWidget(m_unselectBtn);
	row3->addSpacing(20);
	row3->addWidget(m_removeBtn);
	row3->addStretch();
	jl->addLayout(row3);

	jl->addSpacing(6);
	jl->addWidget(new QLabel("Output Folder"));
	auto *row4 = new QHBoxLayout;
	m_browseOutputBtn = new QPushButton("Select output folder");
	m_browseOutputBtn->setToolTip("Choose where finished output files will be saved");
	m_outputExtBtn = new QPushButton("Select output file type");
	m_outputExtBtn->setToolTip("Choose which output file type this job should create");
	m_outputExtMenu = new QMenu(m_outputExtBtn);
	m_outputExtBtn->setMenu(m_outputExtMenu);
	m_outputExtBtn->setMinimumWidth(233);
	row4->addWidget(m_browseOutputBtn);
	row4->addStretch();
	row4->addWidget(m_outputExtBtn);
	jl->addLayout(row4);

	m_outputEdit = new QLineEdit(m_outputFolder);
	jl->addWidget(m_outputEdit);
	m_useInputFolder = new QCheckBox("Save output in input file folders");
	m_useInputFolder->setToolTip("Save output beside each source file instead of using the output folder above");
	m_useInputFolder->setChecked(appSettings().value(keys::UseInputFolderAsOutput, 0).toInt() != 0);
	jl->addWidget(m_useInputFolder);
	main->addWidget(m_jobsGroup, 1);

	// ---- Start / cancel
	auto *btnRow = new QHBoxLayout;
	m_startBtn = new QPushButton("Start all jobs!");
	m_startBtn->setToolTip("Build the queue and start all listed jobs");
	m_startBtn->setMinimumSize(200, 38);
	m_startBtn->setStyleSheet(
		"QPushButton { background:#74b6cc; color:#222; font-weight:bold; border-radius:3px; padding:6px 18px; }"
		"QPushButton:hover { background:#84bed1; color:white; }"
		"QPushButton:pressed { background:#a5d6e6; }"
		"QPushButton:disabled { background:#d3dde0; color:#888; }");
	m_cancelAllBtn = new QPushButton("CANCEL ALL JOBS");
	m_cancelAllBtn->setToolTip("Stop all running and queued jobs");
	m_cancelAllBtn->setMinimumSize(200, 38);
	m_cancelAllBtn->setStyleSheet(
		"QPushButton { background:#fc6d62; color:white; font-weight:bold; border-radius:3px; padding:6px 18px; }"
		"QPushButton:hover { background:#ff8e85; }"
		"QPushButton:pressed { background:#fad5d2; }");
	m_cancelAllBtn->hide();
	btnRow->addStretch();
	btnRow->addWidget(m_startBtn);
	btnRow->addWidget(m_cancelAllBtn);
	btnRow->addStretch();
	main->addLayout(btnRow);

	// ---- options
	m_fileOptGroup = new QGroupBox("File Options", central);
	m_fileOptGrid = new QGridLayout(m_fileOptGroup);
	m_chdmanOptGroup = new QGroupBox("CHDMAN Options", central);
	m_chdmanOptGrid = new QGridLayout(m_chdmanOptGroup);
	main->addWidget(m_fileOptGroup);
	main->addWidget(m_chdmanOptGroup);

	// ---- progress
	m_progressGroup = new QGroupBox("Progress", central);
	auto *pl = new QVBoxLayout(m_progressGroup);
	m_overallBar = new QProgressBar;
	m_overallBar->setRange(0, 100);
	m_overallBar->setTextVisible(true);
	m_overallBar->setMinimumHeight(24);
	m_overallBar->setStyleSheet("QProgressBar { text-align: left; padding-left: 8px; font-weight: bold; }"
	                            "QProgressBar::chunk { background: #3a9d3a; }");
	pl->addWidget(m_overallBar);
	pl->addSpacing(6);
	for (int i = 0; i < JobQueueSizeLimit; ++i) {
		SlotRow r;
		r.row = new QWidget;
		auto *h = new QHBoxLayout(r.row);
		h->setContentsMargins(0, 0, 0, 0);
		r.bar = new QProgressBar;
		r.bar->setRange(0, 100);
		r.bar->setTextVisible(true);
		r.bar->setMinimumHeight(22);
		r.bar->setStyleSheet("QProgressBar { text-align: left; padding-left: 8px; }"
		                     "QProgressBar::chunk { background: #17a2b8; }");
		r.cancel = new QPushButton("X");
		r.cancel->setFixedWidth(28);
		r.cancel->setToolTip("Cancel this individual job");
		h->addWidget(r.bar, 1);
		h->addWidget(r.cancel);
		r.row->hide();
		pl->addWidget(r.row);
		connect(r.cancel, &QPushButton::clicked, this, [this, i] { cancelSlot(i); });
		m_slotRows << r;
	}
	m_progressGroup->hide();
	main->addWidget(m_progressGroup);

	// ---- status bar
	m_statusLabel = new QLabel;
	statusBar()->addWidget(m_statusLabel, 1);
	statusBar()->addPermanentWidget(new QLabel(QString("  namDHC v%1 for CHDMAN  ").arg(NAMDHC_VERSION)));

	// ---- signals
	connect(m_jobCombo, &QComboBox::currentIndexChanged, this, &MainWindow::selectJob);
	connect(m_mediaCombo, &QComboBox::currentIndexChanged, this, &MainWindow::selectMedia);
	connect(m_addFilesBtn, &QPushButton::clicked, this, &MainWindow::onAddFiles);
	connect(m_addFolderBtn, &QPushButton::clicked, this, &MainWindow::onAddFolder);
	connect(m_list, &QListWidget::itemSelectionChanged, this, &MainWindow::updateListButtons);
	connect(m_selectAllBtn, &QPushButton::clicked, m_list, &QListWidget::selectAll);
	connect(m_unselectBtn, &QPushButton::clicked, m_list, &QListWidget::clearSelection);
	connect(m_removeBtn, &QPushButton::clicked, this, &MainWindow::onRemoveSelected);
	connect(m_browseOutputBtn, &QPushButton::clicked, this, &MainWindow::onBrowseOutput);
	connect(m_outputEdit, &QLineEdit::editingFinished, this, &MainWindow::onOutputEdited);
	connect(m_useInputFolder, &QCheckBox::toggled, this, [this](bool on) {
		appSettings().setValue(keys::UseInputFolderAsOutput, on ? 1 : 0);
		m_outputEdit->setEnabled(!on);
		m_browseOutputBtn->setEnabled(!on);
	});
	connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::startJobs);
	connect(m_cancelAllBtn, &QPushButton::clicked, this, &MainWindow::cancelAllJobs);
}

void MainWindow::buildMenus()
{
	QMenu *file = menuBar()->addMenu("&File");
	file->addAction("&Quit", QKeySequence::Quit, this, &QWidget::close);

	QMenu *settings = menuBar()->addMenu("&Settings");
	auto toggle = [&](const QString &text, const char *key, bool def) {
		QAction *a = settings->addAction(text);
		a->setCheckable(true);
		a->setChecked(settingYes(key, def));
		connect(a, &QAction::toggled, this, [key](bool on) { appSettings().setValue(key, yesNo(on)); });
		return a;
	};
	QMenu *jobs = settings->addMenu("Number of jobs to run concurrently");
	auto *group = new QActionGroup(this);
	const int current = qBound(1, appSettings().value(keys::JobQueueSize, DefaultJobQueueSize).toInt(), JobQueueSizeLimit);
	for (int i = 1; i <= JobQueueSizeLimit; ++i) {
		QAction *a = jobs->addAction(QString::number(i));
		a->setCheckable(true);
		a->setChecked(i == current);
		group->addAction(a);
		connect(a, &QAction::triggered, this, [this, i] {
			appSettings().setValue(keys::JobQueueSize, i);
			log("Saved JOB_QUEUE_SIZE");
		});
		m_jobCountActions << a;
	}

	m_actVerbose = toggle("Show a verbose window", keys::ShowVerboseWindow, false);
	connect(m_actVerbose, &QAction::toggled, this, &MainWindow::showVerbose);
	m_actConsole = toggle("Show a chdman console for each new job", keys::ShowJobConsole, false);
	m_actSound = toggle("Play a sound when finished all jobs", keys::PlaySoundFinished, true);
	m_actRemoveOnSuccess = toggle("Remove files from list on success", keys::RemoveFileEntryAfterFinish, true);
	m_actNoDpi = toggle("No DPI scaling", keys::NoDpiScale, false);
	connect(m_actNoDpi, &QAction::toggled, this, [this] {
		QMessageBox::warning(this, NAMDHC_APP_NAME, "You will need to restart to see any changes");
	});
	settings->addAction("Set 7-Zip location...", this, &MainWindow::set7ZipLocation);
	settings->addAction("Set chdman location...", this, &MainWindow::setChdmanLocation);

	QMenu *about = menuBar()->addMenu("&About");
	about->addAction("About", this, [this] { (new AboutDialog(this))->open(); });
}

// ------------------------------------------------------------------ job / media selection

void MainWindow::selectJob()
{
	m_jobType = JobType(m_jobCombo->currentData().toInt());
	QString label;
	QSignalBlocker block(m_mediaCombo);
	m_mediaCombo->clear();
	switch (m_jobType) {
	case JobType::Create:
	case JobType::Extract:
		label = m_jobType == JobType::Create ? "CREATE CHD" : "EXTRACT MEDIA";
		for (const QString &m : mediaKeys())
			m_mediaCombo->addItem(mediaLabel(m), m);
		break;
	case JobType::Info:
		label = "GET INFO";
		m_mediaCombo->addItem("CHD Files", "chd");
		break;
	case JobType::Verify:
		label = "VERIFY CHD";
		m_mediaCombo->addItem("CHD Files", "chd");
		break;
	}
	m_startBtn->setText(label);
	m_mediaCombo->setCurrentIndex(0);
	selectMedia();
}

void MainWindow::selectMedia()
{
	m_media = m_mediaCombo->currentData().toString();
	m_cmd = commandFor(m_jobType, m_media);
	m_spec = commandSpec(m_cmd);

	// zip/7z input only when a tool can open them
	const QString sz = sevenZip();
	QStringList usable;
	for (const QString &e : std::as_const(m_spec.inputExts))
		if (!isArchiveExt(e) || canHandleArchive(e, sz))
			usable << e;
	m_spec.inputExts = usable;

	rebuildOptionWidgets();
	rebuildExtMenus();

	m_list->clear();
	m_list->addItems(m_files.value(m_cmd));
	refreshGui();
}

void MainWindow::rebuildOptionWidgets()
{
	for (OptWidgets &w : m_optWidgets) {
		delete w.check;
		delete w.edit;
		delete w.combo;
	}
	m_optWidgets.clear();

	const OptionDefaults defaults = defaultsFor(m_cmd);
	QList<const ChdmanOption *> fileOpts, chdOpts;
	for (const QString &name : std::as_const(m_spec.options)) {
		const ChdmanOption *o = findOption(name);
		if (!o || o->hidden)
			continue;
		(o->isFileOption ? fileOpts : chdOpts) << o;
	}

	auto fill = [&](QGridLayout *grid, QGroupBox *box, const QList<const ChdmanOption *> &opts) {
		box->setVisible(!opts.isEmpty());
		for (int i = 0; i < opts.size(); ++i) {
			const ChdmanOption *o = opts[i];
			const int col = (i / OptionsPerColumn) * 3;
			const int row = i % OptionsPerColumn;
			OptWidgets w;
			w.check = new QCheckBox(" " + o->description);
			w.check->setToolTip(o->helpText);
			if (o->indent)
				w.check->setStyleSheet(QString("QCheckBox { margin-left: %1px; }").arg(16 * o->indent));
			grid->addWidget(w.check, row, col);
			if (o->kind == OptKind::Edit) {
				w.edit = new QLineEdit(defaults.values.value(o->name, o->defaultValue));
				w.edit->setToolTip(o->helpText);
				w.edit->setMaximumWidth(200);
				grid->addWidget(w.edit, row, col + 1);
			} else if (o->kind == OptKind::Dropdown) {
				w.combo = new QComboBox;
				w.combo->setToolTip(o->helpText);
				for (int k = 0; k < o->dropdownLabels.size(); ++k)
					w.combo->addItem(o->dropdownLabels[k], o->dropdownValues.value(k));
				if (o->name == "numprocessors")
					w.combo->setCurrentIndex(w.combo->count() - 1);
				w.combo->setMaximumWidth(260);
				grid->addWidget(w.combo, row, col + 1);
			}
			w.check->setChecked(defaults.checked.contains(o->name));
			const QString name = o->name;
			connect(w.check, &QCheckBox::clicked, this, [this, name](bool on) { onOptionToggled(name, on); });
			m_optWidgets.insert(o->name, w);
		}
		if (opts.size() > OptionsPerColumn)
			grid->setColumnMinimumWidth(2, 30);
		grid->setColumnStretch(grid->columnCount(), 1);
	};
	fill(m_fileOptGrid, m_fileOptGroup, fileOpts);
	fill(m_chdmanOptGrid, m_chdmanOptGroup, chdOpts);
}

void MainWindow::rebuildExtMenus()
{
	m_inputExtMenu->clear();
	m_outputExtMenu->clear();
	m_selectedInputExts = m_spec.inputExts;
	m_selectedOutputExts.clear();

	for (const QString &ext : std::as_const(m_spec.inputExts)) {
		QAction *a = m_inputExtMenu->addAction(ext);
		a->setCheckable(true);
		a->setChecked(true);
		connect(a, &QAction::triggered, this, [this, a, ext](bool on) {
			if (on) {
				if (!m_selectedInputExts.contains(ext))
					m_selectedInputExts << ext;
			} else {
				m_selectedInputExts.removeAll(ext);
				if (m_selectedInputExts.isEmpty()) {  // at least one type stays selected
					a->setChecked(true);
					m_selectedInputExts << ext;
				}
			}
			pruneQueuedFilesBySelectedInputTypes();
		});
	}
	const QStringList wantedArchives = commandSpec(m_cmd).inputExts;
	for (const QString &a : {QString("zip"), QString("7z")}) {
		if (wantedArchives.contains(a) && !m_spec.inputExts.contains(a)) {
			QAction *na = m_inputExtMenu->addAction(a + (a == "7z" ? " (7-Zip not installed on this system)" : " (unzip/7-Zip not installed on this system)"));
			na->setEnabled(false);
		}
	}
	m_inputExtBtn->setToolTip(sevenZip().isEmpty() && wantedArchives.contains("7z")
		? "Choose which input file types namDHC should include  7z is not installed on this system"
		: "Choose which input file types namDHC should include for this job");

	auto *group = new QActionGroup(m_outputExtMenu);
	group->setExclusive(true);
	for (int i = 0; i < m_spec.outputExts.size(); ++i) {
		const QString ext = m_spec.outputExts[i];
		QAction *a = m_outputExtMenu->addAction(ext);
		a->setCheckable(true);
		group->addAction(a);
		// Only one output type is checked by default when extracting.
		const bool checked = i == 0;
		a->setChecked(checked);
		if (checked)
			m_selectedOutputExts << ext;
		connect(a, &QAction::triggered, this, [this, ext] { m_selectedOutputExts = {ext}; });
	}
}

void MainWindow::refreshGui()
{
	if (m_queue && m_queue->isRunning())
		return;

	const bool noOutput = m_jobType == JobType::Info || m_jobType == JobType::Verify;
	m_jobsGroup->setEnabled(true);
	m_fileOptGroup->setEnabled(true);
	m_chdmanOptGroup->setEnabled(true);
	menuBar()->setEnabled(true);
	m_mediaCombo->setEnabled(!noOutput);
	m_outputExtBtn->setEnabled(!noOutput && !m_spec.outputExts.isEmpty());
	m_inputExtBtn->setEnabled(!noOutput || m_spec.inputExts.size() > 1);
	m_outputEdit->setEnabled(!noOutput && !m_useInputFolder->isChecked());
	m_browseOutputBtn->setEnabled(!noOutput && !m_useInputFolder->isChecked());
	m_useInputFolder->setEnabled(!noOutput);

	// option editors follow their checkbox; slave options follow their master
	for (auto it = m_optWidgets.begin(); it != m_optWidgets.end(); ++it) {
		const bool on = it->check->isChecked();
		if (it->edit)
			it->edit->setEnabled(on);
		if (it->combo)
			it->combo->setEnabled(on);
		const ChdmanOption *o = findOption(it.key());
		if (o && !o->masterOf.isEmpty() && m_optWidgets.contains(o->masterOf))
			m_optWidgets[o->masterOf].check->setEnabled(on);
	}

	m_progressGroup->hide();
	m_cancelAllBtn->hide();
	m_startBtn->show();
	m_startBtn->setEnabled(!filesForCurrentCmd().isEmpty());
	updateListButtons();

	const int n = filesForCurrentCmd().size();
	setStatus(n ? QString("%1 jobs in the %2 queue. Ready to start!").arg(n).arg(m_cmd.toUpper()) : QString("Add files to the job queue to start"));
}

void MainWindow::updateListButtons()
{
	const int sel = m_list->selectedItems().size();
	m_removeBtn->setText(sel > 1 ? "Remove files" : sel == 1 ? "Remove file" : "Remove selection");
	m_removeBtn->setEnabled(sel > 0);
	m_unselectBtn->setEnabled(sel > 0);
	m_selectAllBtn->setEnabled(m_list->count() > 0);
}

void MainWindow::onOptionToggled(const QString &name, bool checked)
{
	if (name == "createSubDir" && !checked && m_cmd.startsWith("extract") && !m_selectedOutputExts.isEmpty()
		&& isMultiFileExtractType(m_selectedOutputExts.first())) {
		const auto answer = QMessageBox::question(this, NAMDHC_APP_NAME,
			"The selected output type '" + m_selectedOutputExts.first()
				+ "' can extract many files into a single folder.\n\nContinue without creating a folder for each job?");
		if (answer != QMessageBox::Yes) {
			m_optWidgets[name].check->setChecked(true);
			return;
		}
	}
	const ChdmanOption *o = findOption(name);
	if (o && !o->masterOf.isEmpty() && m_optWidgets.contains(o->masterOf) && !checked)
		m_optWidgets[o->masterOf].check->setChecked(false);
	refreshGui();
}

// ------------------------------------------------------------------ input files

void MainWindow::reportQueuedFiles()
{
	const int n = filesForCurrentCmd().size();
	const QString msg = n ? QString("%1 jobs in the %2 queue. Ready to start!").arg(n).arg(m_cmd.toUpper())
	                      : QString("No jobs in the %1 file queue").arg(m_cmd.toUpper());
	log(msg);
	setStatus(msg);
}

void MainWindow::onAddFiles()
{
	QSettings s = appSettings();
	const QString startDir = nearestExistingDir(s.value(keys::LastInputBrowseFolder).toString(), defaultBrowseDir());
	QStringList patterns;
	for (const QString &e : std::as_const(m_selectedInputExts))
		patterns << "*." + e << "*." + e.toUpper();
	const QStringList files = QFileDialog::getOpenFileNames(this, "Select files", startDir,
		"Supported files (" + patterns.join(' ') + ");;All files (*)");
	if (files.isEmpty())
		return;
	s.setValue(keys::LastInputBrowseFolder, QFileInfo(files.first()).absolutePath());
	addFiles(files, true);
}

void MainWindow::onAddFolder()
{
	QSettings s = appSettings();
	const QString startDir = nearestExistingDir(s.value(keys::LastInputBrowseFolder).toString(), defaultBrowseDir());
	const QString dir = QFileDialog::getExistingDirectory(this,
		"Select a folder containing " + m_selectedInputExts.join(", ") + " type files.", startDir);
	if (dir.isEmpty())
		return;
	s.setValue(keys::LastInputBrowseFolder, dir);
	setStatus("Scanning folder '" + dir + "'...");
	QApplication::setOverrideCursor(Qt::WaitCursor);
	const QStringList files = scanFolder(dir, m_selectedInputExts, true);
	QApplication::restoreOverrideCursor();
	setStatus("Finished scanning folder. Adding files...");
	addFiles(files, false);
}

void MainWindow::addFiles(const QStringList &newFiles, bool fromFileDialog)
{
	if (newFiles.isEmpty()) {
		reportQueuedFiles();
		return;
	}
	QStringList &queued = m_files[m_cmd];
	const QString sz = sevenZip();
	QStringList innerTypes;
	for (const QString &e : std::as_const(m_selectedInputExts))
		if (!isArchiveExt(e))
			innerTypes << e;
	QStringList badArchives;

	m_jobsGroup->setEnabled(false);
	QApplication::setOverrideCursor(Qt::WaitCursor);
	for (int i = 0; i < newFiles.size(); ++i) {
		const QString file = QFileInfo(newFiles[i]).absoluteFilePath();
		if (i % 100 == 0) {
			setStatus(QString("Adding files... %1/%2").arg(i).arg(newFiles.size()));
			QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
		}
		const QString ext = QFileInfo(file).suffix().toLower();
		if (!m_selectedInputExts.contains(ext))
			continue;
		if (queued.contains(file)) {
			log("Skip adding " + file + "  -  Already in queue");
			continue;
		}
		QString inner;
		if (isArchiveExt(ext)) {
			inner = firstMatchingArchiveEntry(file, innerTypes, sz);
			if (inner.isEmpty()) {
				log("Skip adding " + file + "  -  No selected formats found in archive file");
				if (fromFileDialog)
					badArchives << file;
				continue;
			}
		}
		queued << file;
		m_list->addItem(file);
		const QString msg = "Adding " + (inner.isEmpty() ? file : file + "   -->   " + inner);
		log(msg);
		setStatus(msg);
	}
	QApplication::restoreOverrideCursor();
	m_jobsGroup->setEnabled(true);

	if (!badArchives.isEmpty()) {
		const QString types = innerTypes.isEmpty() ? QString("supported") : innerTypes.join(" or ");
		QMessageBox::warning(this, NAMDHC_APP_NAME,
			badArchives.size() == 1 ? "No " + types + " media was found in the archive file:\n\n" + badArchives.first()
			                        : "No " + types + " media was found in these archive files:\n\n" + badArchives.join('\n'));
	}
	reportQueuedFiles();
	refreshGui();
}

void MainWindow::onRemoveSelected()
{
	QStringList &queued = m_files[m_cmd];
	const QList<QListWidgetItem *> items = m_list->selectedItems();
	for (QListWidgetItem *it : items) {
		queued.removeAll(it->text());
		log("Removed '" + it->text() + "' from the " + m_cmd.toUpper() + " queue");
		setStatus("Removed '" + it->text() + "' from the " + m_cmd.toUpper() + " queue");
		delete it;
	}
	reportQueuedFiles();
	refreshGui();
}

bool MainWindow::queuedFileMatches(const QString &file) const
{
	const QString ext = QFileInfo(file).suffix().toLower();
	if (!m_selectedInputExts.contains(ext))
		return false;
	if (!isArchiveExt(ext))
		return true;
	QStringList inner;
	for (const QString &e : m_selectedInputExts)
		if (!isArchiveExt(e))
			inner << e;
	return !firstMatchingArchiveEntry(file, inner, sevenZip()).isEmpty();
}

void MainWindow::pruneQueuedFilesBySelectedInputTypes()
{
	QStringList &queued = m_files[m_cmd];
	QStringList kept;
	for (const QString &f : std::as_const(queued))
		if (queuedFileMatches(f))
			kept << f;
	const int removed = queued.size() - kept.size();
	if (removed < 1)
		return;
	queued = kept;
	m_list->clear();
	m_list->addItems(kept);
	log(QString("Removed %1 queued file(s) that no longer match the selected input file types").arg(removed));
	reportQueuedFiles();
	refreshGui();
}

void MainWindow::removeFinishedInputEntry(const QString &cmd, const QString &file)
{
	QStringList &queued = m_files[cmd];
	if (!queued.removeAll(file))
		return;
	if (cmd == m_cmd) {
		const QList<QListWidgetItem *> items = m_list->findItems(file, Qt::MatchExactly);
		for (QListWidgetItem *it : items)
			delete it;
	}
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
	if (e->mimeData()->hasUrls() && !(m_queue && m_queue->isRunning()))
		e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e)
{
	QStringList files;
	setStatus("Scanning dropped items...");
	for (const QUrl &u : e->mimeData()->urls()) {
		const QString p = u.toLocalFile();
		if (p.isEmpty())
			continue;
		if (QFileInfo(p).isDir())
			files << scanFolder(p, m_selectedInputExts, true);
		else
			files << p;
	}
	setStatus("Finished scanning dropped items. Adding files...");
	addFiles(files, false);
}

// ------------------------------------------------------------------ output folder

void MainWindow::onBrowseOutput()
{
	QSettings s = appSettings();
	const QString current = m_outputEdit->text().trimmed();
	const QString start = QFileInfo(current).isDir() ? current
	                      : nearestExistingDir(s.value(keys::LastOutputBrowseFolder).toString(), nearestExistingDir(m_outputFolder, QDir::homePath()));
	const QString dir = QFileDialog::getExistingDirectory(this, "Select a folder to save converted files to", start);
	if (dir.isEmpty())
		return;
	s.setValue(keys::LastOutputBrowseFolder, dir);
	m_outputEdit->setText(dir);
	onOutputEdited();
}

void MainWindow::onOutputEdited()
{
	QString folder = m_outputEdit->text().trimmed();
	if (folder.startsWith("~/"))
		folder = QDir::homePath() + folder.mid(1);
	if (folder.isEmpty() || folder == m_outputFolder) {
		m_outputEdit->setText(m_outputFolder);
		return;
	}
	if (!QDir::isAbsolutePath(folder) || folder.contains(QChar('\0')) || folder.size() > 4096) {
		QMessageBox::warning(this, NAMDHC_APP_NAME, "Invalid output folder");
		m_outputEdit->setText(m_outputFolder);
		return;
	}
	m_outputFolder = QDir::cleanPath(folder);
	m_outputEdit->setText(m_outputFolder);
	QSettings s = appSettings();
	s.setValue(keys::OutputFolder, m_outputFolder);
	s.setValue(keys::LastOutputBrowseFolder, m_outputFolder);
	log("'" + m_outputFolder + "' selected as output folder");
	setStatus("'" + m_outputFolder + "' selected as new output folder");
}

// ------------------------------------------------------------------ running jobs

OptionValues MainWindow::collectOptions() const
{
	OptionValues v;
	for (auto it = m_optWidgets.constBegin(); it != m_optWidgets.constEnd(); ++it) {
		if (!it->check->isChecked() || !it->check->isEnabled())
			continue;
		QString value;
		if (it->edit)
			value = it->edit->text().trimmed();
		else if (it->combo)
			value = it->combo->currentData().toString();
		v.insert(it.key(), value);
	}
	return v;
}

void MainWindow::startJobs()
{
	onOutputEdited();
	const QStringList files = filesForCurrentCmd();

	if (m_cmd == "info") {
		if (files.isEmpty()) {
			QMessageBox::critical(this, "Error", "No jobs in the work queue!");
			return;
		}
		log("Getting info from CHD's");
		setStatus("Info for CHD's");
		auto *dlg = new ChdInfoDialog(m_chdman, files, this);
		dlg->setModal(true);
		dlg->show();
		return;
	}

	setStatus("Creating " + m_cmd.toUpper() + " work queue");
	log("Creating " + m_cmd.toUpper() + " work queue");

	BuildRequest req;
	req.cmd = m_cmd;
	req.options = collectOptions();
	req.outputExts = m_selectedOutputExts;
	req.inputExts = m_selectedInputExts;
	req.files = files;
	req.outputFolder = m_outputFolder;
	req.useInputFolder = m_useInputFolder->isChecked();
	QList<Job> queue;
	buildJobs(req, queue, [this](const Job &n, const Job &e, int num) { return askDuplicate(this, n, e, num); });

	if (queue.isEmpty()) {
		log("No jobs found in the work queue");
		QMessageBox::critical(this, "Error", "No jobs in the work queue!");
		return;
	}

	RunnerConfig cfg;
	cfg.chdman = m_chdman;
	cfg.sevenZip = sevenZip();
	cfg.tempDir = QDir::tempPath() + "/namDHC-" + QString::number(QCoreApplication::applicationPid());

	delete m_queue;
	m_queue = new JobQueue(cfg, this);
	const int concurrency = qBound(1, appSettings().value(keys::JobQueueSize, DefaultJobQueueSize).toInt(), JobQueueSizeLimit);
	const int slotCount = qMin(concurrency, int(queue.size()));

	connect(m_queue, &JobQueue::logMessage, this, &MainWindow::log);
	connect(m_queue, &JobQueue::statusMessage, this, &MainWindow::setStatus);
	connect(m_queue, &JobQueue::overallProgress, this, [this](int pct, const QString &text) {
		m_overallBar->setValue(pct);
		m_overallBar->setFormat(text);
	});
	connect(m_queue, &JobQueue::slotStarted, this, [this](int slot, const Job &job) {
		SlotRow &r = m_slotRows[slot];
		r.bar->setRange(0, 100);
		r.bar->setValue(0);
		r.bar->setFormat("Preparing  -  " + job.workingTitle);
		r.cancel->setEnabled(true);
		if (m_actConsole->isChecked()) {
			if (JobConsole *old = m_consoles.value(slot))
				old->close();
			auto *console = new JobConsole(QString(NAMDHC_APP_NAME " - Job %1 - chdman").arg(job.idx));
			console->show();
			m_consoles[slot] = console;
		}
	});
	connect(m_queue, &JobQueue::slotProgress, this, [this](int slot, int pct, const QString &text, bool busy) {
		SlotRow &r = m_slotRows[slot];
		if (busy) {
			r.bar->setRange(0, 0);
		} else {
			r.bar->setRange(0, 100);
			r.bar->setValue(pct);
		}
		r.bar->setFormat(text);
	});
	connect(m_queue, &JobQueue::slotOutput, this, [this](int slot, const QString &text) {
		if (JobConsole *c = m_consoles.value(slot))
			c->append(text);
	});
	connect(m_queue, &JobQueue::jobFinished, this, [this](int slot, const Job &job, JobStatus st) {
		SlotRow &r = m_slotRows[slot];
		r.bar->setRange(0, 100);
		r.bar->setValue(100);
		r.cancel->setEnabled(false);
		if (st == JobStatus::Success && m_actRemoveOnSuccess->isChecked())
			removeFinishedInputEntry(job.cmd, job.fromFileFull);
		if (QPointer<JobConsole> c = m_consoles.take(slot))
			QTimer::singleShot(1500, c, [c] {
				if (c)
					c->close();
			});
	});
	connect(m_queue, &JobQueue::allFinished, this, &MainWindow::onAllFinished);

	setRunning(true);
	for (int i = 0; i < m_slotRows.size(); ++i) {
		m_slotRows[i].row->setVisible(i < slotCount);
		m_slotRows[i].bar->setRange(0, 100);
		m_slotRows[i].bar->setValue(0);
		m_slotRows[i].bar->setFormat(QString());
	}
	m_overallBar->setValue(0);
	m_overallBar->setFormat(QString("0 jobs of %1 completed - 0%").arg(queue.size()));
	log(QString("%1 %2 jobs starting ...").arg(queue.size()).arg(m_cmd.toUpper()));
	setStatus(QString("%1 %2 jobs started").arg(queue.size()).arg(m_cmd.toUpper()));
	m_queue->start(queue, slotCount);
}

void MainWindow::setRunning(bool running)
{
	m_jobsGroup->setEnabled(!running);
	m_fileOptGroup->setEnabled(!running);
	m_chdmanOptGroup->setEnabled(!running);
	menuBar()->setEnabled(!running);
	m_startBtn->setVisible(!running);
	m_cancelAllBtn->setVisible(running);
	m_cancelAllBtn->setEnabled(running);
	m_progressGroup->setVisible(running);
	if (running) {
		// keep the options visible but out of the way
		m_fileOptGroup->hide();
		m_chdmanOptGroup->hide();
	}
}

void MainWindow::cancelSlot(int slot)
{
	if (!m_queue)
		return;
	const JobRunner *r = m_queue->slotRunner(slot);
	if (!r || r->isDone())
		return;
	const Job job = r->job();
	QPointer<JobQueue> q = m_queue;
	const bool yes = confirmAutoClose(this, NAMDHC_APP_NAME,
		QString("Cancel job %1 - %2: %3?").arg(job.idx).arg(job.cmd.toUpper(), job.workingTitle),
		[q, slot, r] { return !q || q->slotRunner(slot) != r || r->isDone(); });
	if (yes && q && q->slotRunner(slot) == r)
		m_queue->cancelSlot(slot);
}

void MainWindow::cancelAllJobs()
{
	if (!m_queue || !m_queue->isRunning())
		return;
	QPointer<JobQueue> q = m_queue;
	if (!confirmAutoClose(this, NAMDHC_APP_NAME, "Are you sure you want to cancel all jobs?", [q] { return !q || !q->isRunning(); }))
		return;
	m_cancelAllBtn->setEnabled(false);
	m_queue->cancelAll();
}

void MainWindow::onAllFinished()
{
	const Tally &t = m_queue->tally();
	const QString summary = m_queue->summaryText(m_spec.finishedText);
	setRunning(false);
	setStatus(t.withError ? "Jobs finished with some errors" : "Jobs finished!");
	log(QString(summary).replace('\n', ", "));

	if (m_quitting) {
		qApp->quit();
		return;
	}
	selectMedia();  // restore the option panels and the list

	if (m_actSound->isChecked())
		playFinishedSound();

	const QString question = t.withError ? "Finished with errors.\nWould you like to see a report?" : "Finished!\nWould you like to see a report?";
	if (QMessageBox::question(this, NAMDHC_APP_NAME, question) == QMessageBox::Yes) {
		const QString savings = m_cmd.startsWith("create") ? m_queue->sizeSavingsText() : QString();
		auto *dlg = new ReportDialog(m_spec.description + " report", summary, savings, m_queue->allReport(), this);
		dlg->show();
	}
}

// ------------------------------------------------------------------ misc

void MainWindow::log(const QString &msg)
{
	if (m_verbose)
		m_verbose->log(msg);
}

void MainWindow::setStatus(const QString &msg)
{
	m_statusLabel->setText(msg);
}

void MainWindow::showVerbose(bool show)
{
	if (show) {
		m_verbose->show();
		m_verbose->raise();
	} else {
		m_verbose->hide();
	}
}

QString MainWindow::sevenZip() const
{
	return locate7Zip(appSettings().value(keys::SevenZipCustomPath).toString());
}

void MainWindow::set7ZipLocation()
{
	const QString current = sevenZip();
	const QString file = QFileDialog::getOpenFileName(this, "Select the 7-Zip executable (7z, 7zz or 7za)",
		current.isEmpty() ? "/usr/bin" : QFileInfo(current).absolutePath());
	if (file.isEmpty())
		return;
	const QString name = QFileInfo(file).fileName();
	if (!(name == "7z" || name == "7zz" || name == "7za") || !QFileInfo(file).isExecutable()) {
		QMessageBox::warning(this, NAMDHC_APP_NAME, "The selected file is not a 7-Zip executable (7z, 7zz or 7za).");
		return;
	}
	appSettings().setValue(keys::SevenZipCustomPath, file);
	selectMedia();
	log("Saved 7-Zip location: " + file);
	setStatus("Saved 7-Zip location: " + file);
}

void MainWindow::setChdmanLocation()
{
	const QString file = QFileDialog::getOpenFileName(this, "Select the chdman executable", QFileInfo(m_chdman).absolutePath());
	if (file.isEmpty())
		return;
	if (!QFileInfo(file).isExecutable()) {
		QMessageBox::warning(this, NAMDHC_APP_NAME, "The selected file is not executable.");
		return;
	}
	appSettings().setValue(keys::ChdmanCustomPath, file);
	m_chdman = file;
	log("Using chdman: " + file);
	setStatus("Saved chdman location: " + file);
}

void MainWindow::moveEvent(QMoveEvent *e)
{
	QMainWindow::moveEvent(e);
	if (isVisible())
		appSettings().setValue(keys::MainWindowGeometry, saveGeometry());
}

void MainWindow::closeEvent(QCloseEvent *e)
{
	appSettings().setValue(keys::MainWindowGeometry, saveGeometry());
	if (m_queue && m_queue->isRunning()) {
		if (QMessageBox::question(this, NAMDHC_APP_NAME, "Are you sure? Currently running jobs will be killed.") != QMessageBox::Yes) {
			e->ignore();
			return;
		}
		// Cancel properly so partial output is cleaned up, then quit.
		m_quitting = true;
		m_queue->cancelAll();
		QTimer::singleShot(8000, qApp, &QApplication::quit);
		e->ignore();
		hide();
		return;
	}
	m_verbose->close();
	e->accept();
	qApp->quit();
}

} // namespace namdhc
