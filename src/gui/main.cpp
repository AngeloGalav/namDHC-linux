// SPDX-License-Identifier: GPL-3.0-or-later
// namdhc-gui - graphical frontend for chdman.
#include "mainwindow.h"
#include "settings.h"
#include "utils.h"
#include "version.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>

using namespace namdhc;

int main(int argc, char *argv[])
{
	// "No DPI scaling" has to be applied before QApplication exists.
	if (appSettings().value(keys::NoDpiScale, "no").toString() == "yes") {
		qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
		qputenv("QT_SCALE_FACTOR", "1");
	}

	QApplication app(argc, argv);
	QApplication::setApplicationName("namDHC");
	QApplication::setApplicationVersion(NAMDHC_VERSION);
	QApplication::setDesktopFileName("io.github.namdhc");
	QApplication::setWindowIcon(QIcon(":/namdhc.svg"));

	QString chdman = locateChdman(appSettings().value(keys::ChdmanCustomPath).toString());
	if (chdman.isEmpty()) {
		const auto answer = QMessageBox::critical(nullptr, "Fatal Error",
			"CHDMAN not found!\n\nMake sure the chdman executable is located in the same directory as namDHC "
			"or in your PATH.\n\nDo you want to select the chdman executable now?",
			QMessageBox::Yes | QMessageBox::No);
		if (answer == QMessageBox::Yes)
			chdman = QFileDialog::getOpenFileName(nullptr, "Select the chdman executable", "/usr/bin");
		if (chdman.isEmpty() || !QFileInfo(chdman).isExecutable())
			return 1;
		appSettings().setValue(keys::ChdmanCustomPath, chdman);
	}

	MainWindow w(chdman);
	w.show();
	return app.exec();
}
