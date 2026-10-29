// SPDX-License-Identifier: GPL-3.0-or-later
#include "options.h"

#include <QThread>

namespace namdhc {

namespace {

ChdmanOption flag(QString name, QString param, QString cli, QString desc, QString help)
{
	ChdmanOption o;
	o.name = std::move(name);
	o.param = std::move(param);
	o.cliName = std::move(cli);
	o.description = std::move(desc);
	o.helpText = std::move(help);
	return o;
}

ChdmanOption edit(QString name, QString param, QString cli, QString desc, QString def, QString help)
{
	ChdmanOption o = flag(std::move(name), std::move(param), std::move(cli), std::move(desc), std::move(help));
	o.kind = OptKind::Edit;
	o.defaultValue = std::move(def);
	return o;
}

ChdmanOption fileOpt(QString name, QString cli, QString desc, QString help)
{
	ChdmanOption o = flag(std::move(name), QString(), std::move(cli), std::move(desc), std::move(help));
	o.isFileOption = true;
	return o;
}

QList<ChdmanOption> buildOptions()
{
	QList<ChdmanOption> opts;

	ChdmanOption force = flag("force", "f", "force", "Force overwriting an existing output file",
		"Overwrite an existing output file instead of skipping the job");
	force.isFileOption = true;  // namDHC shows it with the file options but still passes -f to chdman
	opts << force;
	opts << flag("fixverify", "f", "fix", "Fix the SHA-1 if it is incorrect",
		"Update the CHD SHA-1 when verification finds a mismatch");
	ChdmanOption verbose = flag("verbose", "v", "chdman-verbose", "Verbose output", "Include more CHDMAN details in the report");
	verbose.hidden = true;
	opts << verbose;
	opts << edit("outputbin", "ob", "output-bin", "Output filename for binary data", "filename.bin",
		"Set the BIN filename used when extracting CD media");
	opts << flag("splitbin", "sb", "split-bin", "Output one binary file per track",
		"Write one BIN file for each track instead of one combined BIN");
	opts << edit("inputparent", "ip", "input-parent", "Input Parent", "filename.ext",
		"Use a parent CHD when reading a delta CHD");
	opts << edit("inputstartframe", "isf", "input-start-frame", "Input Start Frame", "0",
		"Start reading at this frame offset");
	opts << edit("inputframes", "if", "input-frames", "Effective length of input in frames", "0",
		"Limit how many input frames are read");
	opts << edit("inputstartbyte", "isb", "input-start-byte", "Starting byte offset within the input", "0",
		"Start reading at this byte offset");
	opts << edit("outputparent", "op", "output-parent", "Parent CHD for output", "parent.chd",
		"Create a delta CHD from this parent CHD without renaming the output file");
	opts << edit("hunksize", "hs", "hunk-size", "Size of each hunk (in bytes)", "19584",
		"Set the CHD hunk size in bytes for the output file");
	opts << edit("inputstarthunk", "ish", "input-start-hunk", "Starting hunk offset within the input", "0",
		"Start reading at this hunk offset");
	opts << edit("inputbytes", "ib", "input-bytes", "Effective length of input (in bytes)", "0",
		"Limit how many input bytes are read");
	opts << edit("compression", "c", "compression", "Compression codecs to use", "cdlz,cdzl,cdfl",
		"Choose the compression codecs CHDMAN should use");
	opts << edit("inputhunks", "ih", "input-hunks", "Effective length of input (in hunks)", "0",
		"Limit how many input hunks are read");

	ChdmanOption np = flag("numprocessors", "np", "num-processors", "Max number of CPU threads to use",
		"Limit how many CPU threads CHDMAN may use");
	np.kind = OptKind::Dropdown;
	const int cores = qMax(1, QThread::idealThreadCount());
	for (int i = 1; i <= cores; ++i) {
		np.dropdownLabels << QString::number(i);
		np.dropdownValues << QString::number(i);
	}
	opts << np;

	ChdmanOption tp = flag("template", "tp", "template", "Hard drive template to use",
		"Use a preset hard disk layout instead of entering it manually");
	tp.kind = OptKind::Dropdown;
	tp.dropdownLabels = hdTemplateLabels();
	for (int i = 0; i < tp.dropdownLabels.size(); ++i)
		tp.dropdownValues << QString::number(i);
	opts << tp;

	opts << edit("chs", "chs", "chs", "CHS Values [cyl, heads, sectors]", "332,16,63",
		"Set hard disk geometry as cylinders, heads, sectors");
	opts << edit("ident", "id", "ident", "Name of ident file for CHS info", "filename.chs",
		"Load hard disk geometry from an ident file");
	opts << edit("size", "s", "size", "Size of output file (in bytes)", "0",
		"Set the output size in bytes when creating a blank hard disk image");
	opts << edit("unitsize", "us", "unit-size", "Size of each unit (in bytes)", "512",
		"Set the addressable unit size in bytes for raw CHDs without a parent");
	opts << edit("sectorsize", "ss", "sector-size", "Size of each hard disk sector (in bytes)", "512",
		"Set the hard disk sector size in bytes");

	ChdmanOption delFiles = fileOpt("deleteInputFiles", "delete-input", "Delete input files after completing job",
		"Delete the source files after a successful job");
	delFiles.masterOf = "deleteInputDir";
	opts << delFiles;
	ChdmanOption delDir = fileOpt("deleteInputDir", "delete-input-dir", "Also delete input directory (if empty)",
		"Also remove the source folder if it is empty after deleting the source files");
	delDir.indent = 1;
	opts << delDir;
	opts << fileOpt("createSubDir", "subdir", "Create a folder for each successful job",
		"Save each job's output files in a separate folder");
	opts << fileOpt("keepIncomplete", "keep-incomplete", "Keep failed or cancelled output files",
		"Keep partial output files when a job fails or is cancelled");
	return opts;
}

} // namespace

const QList<ChdmanOption> &allOptions()
{
	static const QList<ChdmanOption> opts = buildOptions();
	return opts;
}

const ChdmanOption *findOption(const QString &name)
{
	for (const ChdmanOption &o : allOptions())
		if (o.name == name)
			return &o;
	return nullptr;
}

const ChdmanOption *findOptionByCli(const QString &cliName)
{
	for (const ChdmanOption &o : allOptions())
		if (o.cliName == cliName)
			return &o;
	return nullptr;
}

QString jobTypeLabel(JobType t)
{
	switch (t) {
	case JobType::Create: return QStringLiteral("Create CHD files from media");
	case JobType::Extract: return QStringLiteral("Extract images from CHD files");
	case JobType::Info: return QStringLiteral("Get info from CHD files");
	case JobType::Verify: return QStringLiteral("Verify CHD files");
	}
	return {};
}

QString jobTypeKey(JobType t)
{
	switch (t) {
	case JobType::Create: return QStringLiteral("create");
	case JobType::Extract: return QStringLiteral("extract");
	case JobType::Info: return QStringLiteral("info");
	case JobType::Verify: return QStringLiteral("verify");
	}
	return {};
}

QStringList mediaKeys()
{
	return {"cd", "dvd", "hd", "ld", "raw"};
}

QString mediaLabel(const QString &media)
{
	if (media == "cd") return QStringLiteral("CD image");
	if (media == "dvd") return QStringLiteral("DVD image");
	if (media == "hd") return QStringLiteral("Hard disk image");
	if (media == "ld") return QStringLiteral("LaserDisc image");
	if (media == "raw") return QStringLiteral("Raw image");
	return QStringLiteral("CHD Files");
}

QString commandFor(JobType t, const QString &media)
{
	switch (t) {
	case JobType::Create: return "create" + media;
	case JobType::Extract: return "extract" + media;
	case JobType::Info: return QStringLiteral("info");
	case JobType::Verify: return QStringLiteral("verify");
	}
	return {};
}

CommandSpec commandSpec(const QString &cmd)
{
	static const QStringList extractCommon = {"force", "createSubDir", "deleteInputFiles", "keepIncomplete"};
	static const QStringList createCommon = {"force", "createSubDir", "deleteInputFiles", "deleteInputDir", "keepIncomplete", "numprocessors", "outputparent"};
	static const QStringList byteRange = {"inputstartbyte", "inputstarthunk", "inputbytes", "inputhunks"};

	CommandSpec s;
	s.cmd = cmd;
	const QString media = cmd.startsWith("create") ? cmd.mid(6) : cmd.startsWith("extract") ? cmd.mid(7) : QString();
	if (cmd.startsWith("create")) {
		s.description = "Create CHD from a " + media.toUpper() + " image";
		s.finishedText = "Jobs created";
		s.outputExts = {"chd"};
	} else if (cmd.startsWith("extract")) {
		s.description = "Extract a " + media.toUpper() + " image from CHD";
		s.finishedText = "Jobs extracted";
		s.inputExts = {"chd", "zip", "7z"};
	}

	if (cmd == "extractcd") {
		s.outputExts = {"cue", "toc", "gdi"};
		s.options = extractCommon + QStringList{"outputbin", "splitbin", "inputparent"};
	} else if (cmd == "extractdvd") {
		s.outputExts = {"iso"};
		s.options = extractCommon + QStringList{"inputparent"} + byteRange;
	} else if (cmd == "extractld") {
		s.outputExts = {"raw"};
		s.options = extractCommon + QStringList{"inputparent", "inputstartframe", "inputframes"};
	} else if (cmd == "extracthd") {
		s.outputExts = {"img"};
		s.options = extractCommon + QStringList{"inputparent"} + byteRange;
	} else if (cmd == "extractraw") {
		s.outputExts = {"img", "raw"};
		s.options = extractCommon + QStringList{"inputparent"} + byteRange;
	} else if (cmd == "createcd") {
		s.inputExts = {"cue", "toc", "gdi", "iso", "zip", "7z"};
		s.options = createCommon + QStringList{"hunksize", "compression"};
	} else if (cmd == "createdvd") {
		s.inputExts = {"iso", "zip", "7z"};
		s.options = createCommon + byteRange + QStringList{"hunksize", "compression"};
	} else if (cmd == "createld") {
		s.inputExts = {"raw", "zip", "7z"};
		s.options = createCommon + QStringList{"inputstartframe", "inputframes", "hunksize", "compression"};
	} else if (cmd == "createhd") {
		s.inputExts = {"img", "zip", "7z"};
		s.options = createCommon + QStringList{"compression", "size"} + byteRange
			+ QStringList{"hunksize", "ident", "template", "chs", "sectorsize"};
	} else if (cmd == "createraw") {
		s.inputExts = {"img", "raw", "zip", "7z"};
		s.options = createCommon + byteRange + QStringList{"hunksize", "unitsize", "compression"};
	} else if (cmd == "info") {
		s.inputExts = {"chd"};
		s.description = "Get info from CHD";
		s.finishedText = "Read info from jobs";
	} else if (cmd == "verify") {
		s.inputExts = {"chd", "zip", "7z"};
		s.options = {"inputparent", "fixverify"};
		s.description = "Verify CHD";
		s.finishedText = "Jobs verified";
	}
	return s;
}

bool isKnownCommand(const QString &cmd)
{
	return !commandSpec(cmd).inputExts.isEmpty();
}

OptionDefaults defaultsFor(const QString &cmd)
{
	OptionDefaults d;
	for (const ChdmanOption &o : allOptions())
		if (o.kind == OptKind::Edit)
			d.values.insert(o.name, o.defaultValue);
	d.values["compression"] = "cdlz,cdzl,cdfl";
	d.values["hunksize"] = "19584";
	d.values["unitsize"] = "512";

	if (cmd == "createdvd" || cmd == "createhd" || cmd == "createraw") {
		d.values["compression"] = "lzma,zlib,huff,flac";
		d.values["hunksize"] = "4096";
	} else if (cmd == "createld") {
		d.values["compression"] = "avhu";
		d.values["hunksize"] = "";
	}
	if (cmd == "createraw")
		d.checked << "hunksize" << "unitsize";
	if (cmd.startsWith("extract"))
		d.checked << "createSubDir";
	return d;
}

QStringList hdTemplateLabels()
{
	return {
		"Conner CFA170A  -  163MB",
		"Rodime R0201  -  5MB",
		"Rodime R0202  -  10MB",
		"Rodime R0203  -  15MB",
		"Rodime R0204  -  20MB",
		"Seagate ST-213  -  10MB",
		"Seagate ST-225  -  20MB",
		"Seagate ST-251  -  40MB",
		"Seagate ST-3600N  -  487MB",
		"Maxtor LXT-213S  -  238MB",
		"Maxtor LXT-340S  -  376MB",
		"Maxtor MXT-540SL  -  733MB",
		"Micropolis 1528  -  1272MB",
		"Quantum Fireball CR 4.3 AT  -  4110MB",
		"Quantum Fireball CR 6.4 AT  -  6149MB",
		"Quantum Fireball CR 8.4 AT  -  8063MB",
		"Quantum Fireball CR 13.0 AT  -  12416MB",
	};
}

bool isArchiveExt(const QString &ext)
{
	const QString e = ext.toLower();
	return e == "zip" || e == "7z";
}

bool isMultiFileExtractType(const QString &ext)
{
	const QString e = ext.toLower();
	return e == "cue" || e == "toc" || e == "gdi";
}

} // namespace namdhc
