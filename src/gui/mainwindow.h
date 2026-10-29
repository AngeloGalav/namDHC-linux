// SPDX-License-Identifier: GPL-3.0-or-later
// namDHC main window.
#pragma once

#include "jobqueue.h"
#include "options.h"

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QVector>

class QAction;
class QCheckBox;
class QComboBox;
class QGridLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QProgressBar;
class QPushButton;

namespace namdhc {

class JobConsole;
class VerboseWindow;

class MainWindow : public QMainWindow {
	Q_OBJECT
public:
	explicit MainWindow(const QString &chdman, QWidget *parent = nullptr);
	~MainWindow() override;


protected:
	void closeEvent(QCloseEvent *e) override;
	void dragEnterEvent(QDragEnterEvent *e) override;
	void dropEvent(QDropEvent *e) override;
	void moveEvent(QMoveEvent *e) override;

private:
	struct OptWidgets {
		QCheckBox *check = nullptr;
		QLineEdit *edit = nullptr;
		QComboBox *combo = nullptr;
	};
	struct SlotRow {
		QWidget *row = nullptr;
		QProgressBar *bar = nullptr;
		QPushButton *cancel = nullptr;
	};

	void buildUi();
	void buildMenus();

	void selectJob();
	void selectMedia();
	void refreshGui();
	void rebuildOptionWidgets();
	void rebuildExtMenus();
	void updateListButtons();
	void reportQueuedFiles();

	void addFiles(const QStringList &files, bool fromFileDialog);
	void onAddFiles();
	void onAddFolder();
	void onRemoveSelected();
	void pruneQueuedFilesBySelectedInputTypes();
	bool queuedFileMatches(const QString &file) const;

	void onBrowseOutput();
	void onOutputEdited();
	void onOptionToggled(const QString &name, bool checked);

	void startJobs();
	void cancelAllJobs();
	void cancelSlot(int slot);
	void onAllFinished();
	void setRunning(bool running);
	OptionValues collectOptions() const;
	void removeFinishedInputEntry(const QString &cmd, const QString &file);

	void log(const QString &msg);
	void setStatus(const QString &msg);
	void showVerbose(bool show);
	void set7ZipLocation();
	void setChdmanLocation();
	QString sevenZip() const;
	QStringList filesForCurrentCmd() const { return m_files.value(m_cmd); }

	QString m_chdman;
	JobType m_jobType = JobType::Create;
	QString m_media = "cd";
	QString m_cmd = "createcd";
	CommandSpec m_spec;
	QHash<QString, QStringList> m_files;   // queued input files per chdman command
	QStringList m_selectedInputExts;
	QStringList m_selectedOutputExts;
	QHash<QString, OptWidgets> m_optWidgets;

	// widgets
	QComboBox *m_jobCombo = nullptr;
	QComboBox *m_mediaCombo = nullptr;
	QPushButton *m_addFilesBtn = nullptr;
	QPushButton *m_addFolderBtn = nullptr;
	QPushButton *m_inputExtBtn = nullptr;
	QMenu *m_inputExtMenu = nullptr;
	QListWidget *m_list = nullptr;
	QPushButton *m_selectAllBtn = nullptr;
	QPushButton *m_unselectBtn = nullptr;
	QPushButton *m_removeBtn = nullptr;
	QPushButton *m_browseOutputBtn = nullptr;
	QPushButton *m_outputExtBtn = nullptr;
	QMenu *m_outputExtMenu = nullptr;
	QLineEdit *m_outputEdit = nullptr;
	QCheckBox *m_useInputFolder = nullptr;
	QPushButton *m_startBtn = nullptr;
	QPushButton *m_cancelAllBtn = nullptr;
	QGroupBox *m_jobsGroup = nullptr;
	QGroupBox *m_fileOptGroup = nullptr;
	QGroupBox *m_chdmanOptGroup = nullptr;
	QGridLayout *m_fileOptGrid = nullptr;
	QGridLayout *m_chdmanOptGrid = nullptr;
	QGroupBox *m_progressGroup = nullptr;
	QProgressBar *m_overallBar = nullptr;
	QVector<SlotRow> m_slotRows;
	QLabel *m_statusLabel = nullptr;

	// menus / settings
	QAction *m_actVerbose = nullptr;
	QAction *m_actConsole = nullptr;
	QAction *m_actSound = nullptr;
	QAction *m_actRemoveOnSuccess = nullptr;
	QAction *m_actNoDpi = nullptr;
	QVector<QAction *> m_jobCountActions;

	VerboseWindow *m_verbose = nullptr;
	QHash<int, QPointer<JobConsole>> m_consoles;

	JobQueue *m_queue = nullptr;
	QString m_outputFolder;
	bool m_quitting = false;
};

} // namespace namdhc
