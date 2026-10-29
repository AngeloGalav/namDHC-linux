// SPDX-License-Identifier: GPL-3.0-or-later
// Job description and work-queue construction (namDHC createJob()).
#pragma once

#include "options.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace namdhc {

struct Job {
	int idx = 0;
	QString cmd;               // chdman command
	QStringList cmdArgs;       // chdman options (without -i/-o)
	QStringList inputFileTypes;

	QString workingDir;
	QString fromFileFull;
	QString fromFile;
	QString fromFileNoExt;
	QString fromFileExt;

	QString outputFolder;      // empty for info/verify
	QString toFileNoExt;
	QString toFileExt;
	QString toFile;
	QString toFileFull;

	bool outputFolderIsPerJob = false;
	bool deleteInputFiles = false;
	bool deleteInputDir = false;
	bool keepIncomplete = false;
	bool force = false;

	QString workingTitle;
};

enum class DuplicateAction { Rename, Skip, RenameAll };

// Called when two jobs would write the same output file. `number` is the
// suffix that would be appended ("game [#2].chd").
using DuplicateHandler = std::function<DuplicateAction(const Job &newJob, const Job &existing, int number)>;

struct BuildRequest {
	QString cmd;
	OptionValues options;        // checked options (name -> value)
	QStringList outputExts;      // selected output extensions
	QStringList inputExts;       // selected input extensions
	QStringList files;
	QString outputFolder;        // used unless useInputFolder
	bool useInputFolder = false;
};

// Turn the checked options into chdman command line arguments.
QStringList buildChdmanArgs(const QString &cmd, const OptionValues &options);

// Append the jobs for `req` to `queue`, renaming or skipping duplicates as the
// handler decides. Without a handler duplicates are renamed.
void buildJobs(const BuildRequest &req, QList<Job> &queue, const DuplicateHandler &onDuplicate = {});

} // namespace namdhc
