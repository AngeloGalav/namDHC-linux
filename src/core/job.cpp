// SPDX-License-Identifier: GPL-3.0-or-later
#include "job.h"

#include "utils.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>

namespace namdhc {

QStringList buildChdmanArgs(const QString &cmd, const OptionValues &options)
{
	const CommandSpec spec = commandSpec(cmd);
	QStringList args;
	bool haveHunk = false, haveUnit = false, haveParent = false;

	for (const QString &name : spec.options) {
		const ChdmanOption *opt = findOption(name);
		if (!opt || opt->param.isEmpty() || !options.contains(name))
			continue;
		if (opt->kind == OptKind::Flag) {
			args << "-" + opt->param;
		} else {
			const QString value = options.value(name).trimmed();
			if (value.isEmpty())
				continue;
			args << "-" + opt->param << value;
		}
		haveHunk |= name == "hunksize";
		haveUnit |= name == "unitsize";
		haveParent |= name == "outputparent";
	}

	// createraw requires a hunk size and (without a parent) a unit size.
	if (cmd == "createraw") {
		const OptionDefaults d = defaultsFor(cmd);
		if (!haveHunk)
			args << "-hs" << d.values.value("hunksize");
		if (!haveUnit && !haveParent)
			args << "-us" << d.values.value("unitsize");
	}
	return args;
}

void buildJobs(const BuildRequest &req, QList<Job> &queue, const DuplicateHandler &onDuplicate)
{
	const QStringList args = buildChdmanArgs(req.cmd, req.options);
	const QString outputParent = req.options.contains("outputparent") ? req.options.value("outputparent").trimmed() : QString();
	const bool producesOutput = req.cmd != "verify" && req.cmd != "info";
	const QStringList outputExts = producesOutput ? req.outputExts : QStringList{QString()};
	bool renameAll = false;
	QHash<QString, int> dupCount;

	for (const QString &fromFileFull : req.files) {
		const SplitPath from = splitFilePath(fromFileFull);
		for (const QString &toExt : outputExts) {
			Job q;
			q.idx = queue.size() + 1;
			q.cmd = req.cmd;
			q.cmdArgs = args;
			q.inputFileTypes = req.inputExts;
			q.deleteInputFiles = req.options.contains("deleteInputFiles");
			q.deleteInputDir = q.deleteInputFiles && req.options.contains("deleteInputDir");
			q.keepIncomplete = req.options.contains("keepIncomplete");
			q.outputFolderIsPerJob = req.options.contains("createSubDir");
			q.force = req.options.contains("force");
			q.workingDir = from.dir;
			q.fromFileFull = QFileInfo(fromFileFull).absoluteFilePath();
			q.fromFile = from.file;
			q.fromFileNoExt = from.noExt;
			q.fromFileExt = from.ext;

			bool skip = false;
			if (producesOutput) {
				const QString baseFolder = QDir::cleanPath(req.useInputFolder ? from.dir : req.outputFolder);
				auto place = [&](const QString &noExt) {
					q.toFileNoExt = noExt;
					q.outputFolder = q.outputFolderIsPerJob ? baseFolder + "/" + noExt : baseFolder;
					q.toFileExt = toExt;
					q.toFile = noExt + "." + toExt;
					q.toFileFull = q.outputFolder + "/" + q.toFile;
				};
				place(outputExts.size() > 1 ? from.noExt + " (" + toExt.toUpper() + ")" : from.noExt);

				// The same output name from two inputs (e.g. game.cue and game.iso):
				// suffix " [#n]" or skip, as the user decides.
				for (const Job &existing : std::as_const(queue)) {
					if (existing.toFileFull != q.toFileFull)
						continue;
					const QString key = q.toFileFull;
					const int number = dupCount.value(key, 1) + 1;
					DuplicateAction action = DuplicateAction::Rename;
					if (!renameAll && onDuplicate) {
						action = onDuplicate(q, existing, number);
						if (action == DuplicateAction::RenameAll)
							renameAll = true;
					}
					if (action == DuplicateAction::Skip) {
						skip = true;
					} else {
						dupCount[key] = number;
						place(q.toFileNoExt + " [#" + QString::number(number) + "]");
					}
					break;
				}

				// A parent given as a bare filename lives next to the output.
				if (!skip && !outputParent.isEmpty() && !outputParent.contains('/')) {
					const int i = q.cmdArgs.indexOf("-op");
					if (i >= 0 && i + 1 < q.cmdArgs.size())
						q.cmdArgs[i + 1] = q.outputFolder + "/" + outputParent;
				}
			}
			if (skip)
				continue;
			q.workingTitle = q.toFile.isEmpty() ? q.fromFile : q.toFile;
			queue << q;
		}
	}
}

} // namespace namdhc
