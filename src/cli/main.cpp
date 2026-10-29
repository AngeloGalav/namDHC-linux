// SPDX-License-Identifier: GPL-3.0-or-later
// namdhc - command line frontend for chdman with batch/folder conversion.
#include "chdinfo.h"
#include "jobqueue.h"
#include "options.h"
#include "settings.h"
#include "utils.h"
#include "version.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSocketNotifier>
#include <QTimer>

#include <csignal>
#include <cstdio>
#include <sys/ioctl.h>
#include <unistd.h>

using namespace namdhc;

namespace {

// ISOs bigger than this are treated as DVDs in auto mode (largest CDs are ~870 MB).
constexpr qint64 CdIsoMaxBytes = 900LL * 1024 * 1024;

int g_sigPipe[2] = {-1, -1};

void onSignal(int)
{
	const char c = 1;
	[[maybe_unused]] auto r = ::write(g_sigPipe[1], &c, 1);
}

void out(const QString &s)
{
	fputs(s.toUtf8().constData(), stdout);
	fflush(stdout);
}

void err(const QString &s)
{
	fputs(s.toUtf8().constData(), stderr);
	fflush(stderr);
}

QString usage()
{
	return QString(R"(namDHC %1 - batch frontend for MAME's chdman

Usage:
  namdhc [COMMAND] [MEDIA] [OPTIONS] [FILES or FOLDERS...]
  namdhc --folder DIR                     convert every supported image in DIR to CHD
  namdhc chdman ARGS...                   run the bundled chdman directly

Commands:
  create   (default) create CHD files from images
  extract  extract images from CHD files
  verify   verify CHD files
  info     show information about CHD files

Media (default: auto):
  auto     create:  .cue/.toc/.gdi -> CD, .iso -> CD up to 900 MiB, DVD above
           extract: detected from the CHD metadata
  cd, dvd, hd, ld, raw

Input:
  -f, --folder DIR          add every matching file inside DIR (repeatable)
  -R, --no-recursive        do not descend into subfolders
  -i, --input-types LIST    only these input extensions (e.g. cue,iso)

Output:
  -o, --output DIR          output folder (default: next to each input file)
  -t, --to LIST             output type(s) when extracting (cue,toc,gdi | iso | img,raw)
      --subdir              create a folder for each job (default for extract)
      --no-subdir           never create a folder per job
      --force               overwrite existing output files
      --delete-input        delete input files after a successful job
      --delete-input-dir    also delete the input folder if it ends up empty
      --keep-incomplete     keep failed or cancelled output files
      --on-duplicate MODE   rename (default) or skip jobs with the same output name

chdman options:
  -c, --compression LIST    compression codecs (e.g. cdlz,cdzl,cdfl)
      --hunk-size N         --unit-size N           --num-processors N
      --input-parent FILE   --output-parent FILE    --output-bin NAME   --split-bin
      --input-start-byte N  --input-start-hunk N    --input-bytes N     --input-hunks N
      --input-start-frame N --input-frames N
      --size N  --chs C,H,S  --ident FILE  --template ID  --sector-size N   (hard disks)
      --fix                 fix the SHA-1 when verifying

General:
  -j, --jobs N              jobs to run at the same time (default: %2)
  -n, --dry-run             show the jobs and chdman command lines, run nothing
  -v, --verbose             show the job log and the full report
  -q, --quiet               only print errors and the final summary
      --report FILE         write the full report to FILE
      --chdman PATH         chdman executable to use
      --7z PATH             7-Zip executable to use for .7z/.zip input
      --gui                 open the graphical interface
  -h, --help                show this help
      --version             show version information

Examples:
  namdhc --folder .                       every .cue/.gdi/.toc/.iso here -> .chd
  namdhc create dvd -o ~/chd *.iso        PS2/PSP style DVD images
  namdhc extract cd -t gdi game.chd       Dreamcast CHD back to GDI
  namdhc verify -f ~/roms
)")
		.arg(NAMDHC_VERSION)
		.arg(DefaultJobQueueSize);
}

// ------------------------------------------------------------------ live terminal view

class LiveView : public QObject {
public:
	LiveView(bool tty, bool color, bool quiet)
		: m_tty(tty), m_color(color), m_quiet(quiet)
	{
		connect(&m_timer, &QTimer::timeout, this, [this] {
			++m_frame;
			render();
		});
		if (m_tty && !m_quiet)
			m_timer.start(200);
	}

	void setSlots(int n)
	{
		m_slotText.resize(n);
		m_slotPct.resize(n);
		m_slotBusy.resize(n);
	}
	void setSlot(int slot, int pct, const QString &text, bool busy)
	{
		if (slot < 0 || slot >= m_slotText.size())
			return;
		m_slotPct[slot] = pct;
		m_slotText[slot] = text;
		m_slotBusy[slot] = busy;
		if (!m_tty && !m_quiet)
			return;  // line mode only prints results
	}
	void setOverall(int pct, const QString &text)
	{
		m_overallPct = pct;
		m_overallText = text;
	}

	QString paint(const QString &s, const char *code) const
	{
		return m_color ? QString("\033[%1m%2\033[0m").arg(code, s) : s;
	}

	void print(const QString &line)
	{
		clear();
		out(line + "\n");
		render();
	}

	void finish()
	{
		m_timer.stop();
		clear();
		m_active = false;
	}

	void render()
	{
		if (!m_tty || m_quiet || !m_active)
			return;
		clear();
		const int width = terminalWidth();
		QStringList lines;
		lines << fit(bar(m_overallPct, false, 24) + " " + m_overallText, width);
		for (int i = 0; i < m_slotText.size(); ++i) {
			if (m_slotText[i].isEmpty())
				continue;
			lines << fit(QString(" %1 ").arg(i + 1) + bar(m_slotPct[i], m_slotBusy[i], 20) + " " + m_slotText[i], width);
		}
		QString buf;
		for (const QString &l : lines)
			buf += l + "\n";
		out(buf);
		m_drawn = lines.size();
	}

private:
	void clear()
	{
		if (!m_tty || m_drawn == 0)
			return;
		QString s;
		for (int i = 0; i < m_drawn; ++i)
			s += "\033[1A\033[2K";
		out(s + "\r");
		m_drawn = 0;
	}

	QString bar(int pct, bool busy, int w) const
	{
		QString b;
		if (busy) {
			const int pos = m_frame % (w + 4);
			for (int i = 0; i < w; ++i)
				b += (i >= pos - 4 && i < pos) ? QChar(0x2588) : QChar(0x2591);
			return "[" + paint(b, "36") + "]     ";
		}
		const int filled = qBound(0, pct * w / 100, w);
		b = QString(filled, QChar(0x2588)) + QString(w - filled, QChar(0x2591));
		return "[" + paint(b, "32") + "] " + QString("%1%").arg(pct, 3);
	}

	static int terminalWidth()
	{
		winsize ws{};
		if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 20)
			return ws.ws_col;
		return 100;
	}

	QString fit(const QString &s, int width) const
	{
		// Escape sequences do not take up columns; count only visible characters.
		int visible = 0;
		QString outStr;
		bool esc = false;
		for (const QChar c : s) {
			if (c == '\033')
				esc = true;
			if (!esc) {
				if (visible >= width - 1)
					break;
				++visible;
			}
			outStr += c;
			if (esc && c == 'm')
				esc = false;
		}
		return m_color ? outStr + "\033[0m" : outStr;
	}

	bool m_tty, m_color, m_quiet;
	bool m_active = true;
	int m_drawn = 0;
	int m_frame = 0;
	QTimer m_timer;
	QVector<QString> m_slotText;
	QVector<int> m_slotPct;
	QVector<bool> m_slotBusy;
	int m_overallPct = 0;
	QString m_overallText;
};

// ------------------------------------------------------------------ helpers

QStringList splitList(const QStringList &values)
{
	QStringList out;
	for (const QString &v : values)
		for (const QString &p : v.split(',', Qt::SkipEmptyParts))
			out << p.trimmed().toLower().remove(QChar('.'));
	return out;
}

QString autoCreateMedia(const QString &file, const QString &sevenZip)
{
	QString ext = QFileInfo(file).suffix().toLower();
	qint64 size = QFileInfo(file).size();
	if (isArchiveExt(ext)) {
		ext.clear();
		for (const ArchiveEntry &e : listArchiveEntries(file, sevenZip)) {
			const QString entryExt = QFileInfo(e.path).suffix().toLower();
			if (QStringList{"cue", "toc", "gdi", "iso"}.contains(entryExt)) {
				ext = entryExt;
				size = e.size;
				break;
			}
		}
	}
	if (ext == "cue" || ext == "toc" || ext == "gdi")
		return "cd";
	if (ext == "iso")
		return size > CdIsoMaxBytes ? "dvd" : "cd";
	if (ext == "img")
		return "hd";
	if (ext == "raw")
		return "raw";
	return {};
}

} // namespace

int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);
	QCoreApplication::setApplicationName("namdhc");
	QCoreApplication::setApplicationVersion(NAMDHC_VERSION);

	QStringList rawArgs = app.arguments();

	// `namdhc chdman ...` runs chdman itself.
	if (rawArgs.size() > 1 && rawArgs[1] == "chdman") {
		const QString chdman = locateChdman(appSettings().value(keys::ChdmanCustomPath).toString());
		if (chdman.isEmpty()) {
			err("namdhc: chdman not found\n");
			return 127;
		}
		std::vector<QByteArray> store;
		std::vector<char *> cargs;
		store.push_back(QFile::encodeName(chdman));
		for (int i = 2; i < rawArgs.size(); ++i)
			store.push_back(QFile::encodeName(rawArgs[i]));
		for (QByteArray &b : store)
			cargs.push_back(b.data());
		cargs.push_back(nullptr);
		execv(cargs[0], cargs.data());
		err("namdhc: unable to run " + chdman + "\n");
		return 127;
	}

	QCommandLineParser parser;
	parser.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsOptions);
	const QCommandLineOption optFolder({"f", "folder"}, "", "dir");
	const QCommandLineOption optNoRecursive({"R", "no-recursive"}, "");
	const QCommandLineOption optInputTypes({"i", "input-types"}, "", "list");
	const QCommandLineOption optOutput({"o", "output"}, "", "dir");
	const QCommandLineOption optTo({"t", "to"}, "", "list");
	const QCommandLineOption optNoSubdir("no-subdir", "");
	const QCommandLineOption optDuplicate("on-duplicate", "", "mode", "rename");
	const QCommandLineOption optJobs({"j", "jobs"}, "", "n");
	const QCommandLineOption optDryRun({"n", "dry-run"}, "");
	const QCommandLineOption optVerbose({"v", "verbose"}, "");
	const QCommandLineOption optQuiet({"q", "quiet"}, "");
	const QCommandLineOption optReport("report", "", "file");
	const QCommandLineOption optChdman("chdman", "", "path");
	const QCommandLineOption opt7z("7z", "", "path");
	const QCommandLineOption optGui("gui", "");
	const QCommandLineOption optHelp({"h", "help"}, "");
	const QCommandLineOption optVersion("version", "");
	const QCommandLineOption optCompressionShort("c", "", "list");
	parser.addOptions({optFolder, optNoRecursive, optInputTypes, optOutput, optTo, optNoSubdir, optDuplicate, optJobs,
		optDryRun, optVerbose, optQuiet, optReport, optChdman, opt7z, optGui, optHelp, optVersion, optCompressionShort});

	QHash<QString, QCommandLineOption> chdOpts;  // option name -> parser option
	for (const ChdmanOption &o : allOptions()) {
		if (o.hidden || o.cliName.isEmpty())
			continue;
		const QCommandLineOption clo = o.kind == OptKind::Flag ? QCommandLineOption(o.cliName, "")
		                                                        : QCommandLineOption(o.cliName, "", "value");
		parser.addOption(clo);
		chdOpts.insert(o.name, clo);
	}

	if (!parser.parse(rawArgs)) {
		err("namdhc: " + parser.errorText() + "\nTry 'namdhc --help'.\n");
		return 2;
	}
	if (parser.isSet(optHelp)) {
		out(usage());
		return 0;
	}
	if (parser.isSet(optVersion)) {
		out(QString("namDHC %1 (Linux)\n").arg(NAMDHC_VERSION));
		const QString chdman = locateChdman(parser.value(optChdman));
		if (!chdman.isEmpty()) {
			QProcess p;
			p.start(chdman, {});
			p.waitForFinished(5000);
			out("chdman: " + chdman + "\n  " + QString::fromUtf8(p.readAllStandardOutput()).section('\n', 0, 0).trimmed() + "\n");
		}
		return 0;
	}
	if (parser.isSet(optGui)) {
		const QString gui = QCoreApplication::applicationDirPath() + "/namdhc-gui";
		if (QProcess::startDetached(QFileInfo::exists(gui) ? gui : "namdhc-gui", {}))
			return 0;
		err("namdhc: unable to start namdhc-gui\n");
		return 1;
	}

	QSettings settings = appSettings();
	const bool quiet = parser.isSet(optQuiet);
	const bool verbose = parser.isSet(optVerbose) && !quiet;
	const bool tty = isatty(STDOUT_FILENO);
	const bool color = tty && qEnvironmentVariableIsEmpty("NO_COLOR");

	// ---- command and media
	QStringList positional = parser.positionalArguments();
	QString command = "create";
	QString media = "auto";
	if (!positional.isEmpty() && QStringList{"create", "extract", "verify", "info"}.contains(positional.first().toLower()))
		command = positional.takeFirst().toLower();
	if (!positional.isEmpty() && (mediaKeys().contains(positional.first().toLower()) || positional.first().toLower() == "auto")
		&& !QFileInfo::exists(positional.first()))
		media = positional.takeFirst().toLower();
	if (command == "verify" || command == "info")
		media.clear();

	const QString chdman = locateChdman(parser.isSet(optChdman) ? parser.value(optChdman) : settings.value(keys::ChdmanCustomPath).toString());
	if (chdman.isEmpty()) {
		err("namdhc: chdman not found. Install it next to namdhc, put it in $PATH or use --chdman.\n");
		return 127;
	}
	const QString sevenZip = locate7Zip(parser.isSet(opt7z) ? parser.value(opt7z) : settings.value(keys::SevenZipCustomPath).toString());

	// ---- accepted input types
	QStringList inputTypes;
	if (command == "create") {
		inputTypes = media == "auto" ? QStringList{"cue", "toc", "gdi", "iso", "zip", "7z"} : commandSpec("create" + media).inputExts;
	} else if (command == "extract") {
		inputTypes = media == "auto" ? QStringList{"chd"} : commandSpec("extract" + media).inputExts;
	} else {
		inputTypes = commandSpec(command).inputExts;
	}
	if (parser.isSet(optInputTypes)) {
		const QStringList wanted = splitList(parser.values(optInputTypes));
		QStringList kept;
		for (const QString &t : inputTypes)
			if (wanted.contains(t))
				kept << t;
		for (const QString &t : wanted)
			if (!inputTypes.contains(t))
				err("namdhc: warning: '" + t + "' is not a valid input type here, ignored\n");
		inputTypes = kept;
	}
	for (const QString &a : {QString("zip"), QString("7z")}) {
		if (inputTypes.contains(a) && !canHandleArchive(a, sevenZip)) {
			inputTypes.removeAll(a);
			if (parser.isSet(optInputTypes))
				err("namdhc: warning: no tool found for ." + a + " archives (install 7-Zip), ignored\n");
		}
	}
	if (inputTypes.isEmpty()) {
		err("namdhc: no usable input types\n");
		return 2;
	}

	// ---- collect input files
	const bool recursive = !parser.isSet(optNoRecursive);
	QStringList files;
	auto addFile = [&](const QString &f) {
		const QString abs = QFileInfo(f).absoluteFilePath();
		if (!files.contains(abs))
			files << abs;
	};
	QStringList dirs = parser.values(optFolder);
	for (const QString &p : std::as_const(positional)) {
		const QFileInfo fi(p);
		if (fi.isDir()) {
			dirs << p;
		} else if (fi.isFile()) {
			if (inputTypes.contains(fi.suffix().toLower()))
				addFile(p);
			else
				err("namdhc: skipping '" + p + "': not a supported " + inputTypes.join("/") + " file\n");
		} else {
			err("namdhc: '" + p + "': no such file or directory\n");
		}
	}
	for (const QString &d : std::as_const(dirs)) {
		if (!QFileInfo(d).isDir()) {
			err("namdhc: '" + d + "' is not a folder\n");
			continue;
		}
		for (const QString &f : scanFolder(d, inputTypes, recursive))
			addFile(f);
	}
	if (files.isEmpty()) {
		if (positional.isEmpty() && dirs.isEmpty()) {
			out(usage());
			return 2;
		}
		err("namdhc: no " + inputTypes.join("/") + " files found\n");
		return 1;
	}

	// ---- info: print chdman info for every file
	if (command == "info") {
		int failures = 0;
		for (const QString &f : std::as_const(files)) {
			if (isArchiveExt(QFileInfo(f).suffix())) {
				err("namdhc: info on archives is not supported, skipping " + f + "\n");
				continue;
			}
			const ChdInfo info = readChdInfo(chdman, f);
			out((color ? "\033[1m" : "") + QFileInfo(f).fileName() + (color ? "\033[0m" : "") + "\n" + drawLine(77) + "\n");
			if (!info.ok) {
				++failures;
				out("Error: " + (info.error.isEmpty() ? QString("unable to read CHD") : info.error) + "\n\n");
				continue;
			}
			for (const auto &kv : info.fields)
				out(QString("%1 %2\n").arg(kv.first + ":", -14).arg(kv.second));
			if (!info.metadata.isEmpty())
				out("\nMetadata:\n" + info.metadata);
			if (!info.compression.isEmpty()) {
				out(QString("\n%1  %2  %3\n").arg("Hunks", 10).arg("Percent", 7).arg("Name"));
				for (const auto &row : info.compression)
					out(QString("%1  %2  %3\n").arg(row.hunks, 10).arg(row.percent, 7).arg(row.name));
			}
			out("\n");
		}
		return failures ? 1 : 0;
	}

	// ---- options
	OptionValues userOptions;
	for (auto it = chdOpts.constBegin(); it != chdOpts.constEnd(); ++it) {
		const ChdmanOption *o = findOption(it.key());
		if (!parser.isSet(it.value()))
			continue;
		userOptions.insert(o->name, o->kind == OptKind::Flag ? QString() : parser.value(it.value()));
	}
	if (parser.isSet(optCompressionShort))
		userOptions.insert("compression", parser.value(optCompressionShort));
	if (userOptions.contains("deleteInputDir"))
		userOptions.insert("deleteInputFiles", QString());
	const bool noSubdir = parser.isSet(optNoSubdir);
	const bool skipDuplicates = parser.value(optDuplicate).toLower() == "skip";

	QString outputFolder;
	if (parser.isSet(optOutput)) {
		outputFolder = QDir(parser.value(optOutput)).absolutePath();
	}
	const QStringList wantedOutputs = splitList(parser.values(optTo));

	// ---- build the work queue
	QList<Job> queue;
	QSet<QString> warnedOptions;
	for (const QString &file : std::as_const(files)) {
		QString thisMedia = media;
		if (command == "create" && media == "auto") {
			thisMedia = autoCreateMedia(file, sevenZip);
			if (thisMedia.isEmpty()) {
				err("namdhc: skipping '" + file + "': no supported image found\n");
				continue;
			}
		} else if (command == "extract" && media == "auto") {
			const ChdInfo info = readChdInfo(chdman, file);
			if (!info.ok) {
				err("namdhc: skipping '" + file + "': " + (info.error.isEmpty() ? QString("not a valid CHD") : info.error) + "\n");
				continue;
			}
			thisMedia = mediaFromChdInfo(info);
		}
		const QString cmd = command == "verify" ? QString("verify") : command + thisMedia;
		const CommandSpec spec = commandSpec(cmd);

		// For archives, only consider those that contain something usable.
		const QString ext = QFileInfo(file).suffix().toLower();
		if (isArchiveExt(ext)) {
			QStringList inner = spec.inputExts;
			inner.removeAll("zip");
			inner.removeAll("7z");
			if (firstMatchingArchiveEntry(file, inner, sevenZip).isEmpty()) {
				err("namdhc: skipping '" + file + "': no " + inner.join("/") + " media found in archive\n");
				continue;
			}
		} else if (!spec.inputExts.contains(ext)) {
			err("namdhc: skipping '" + file + "': " + cmd + " does not accept ." + ext + " files\n");
			continue;
		}

		const OptionDefaults defaults = defaultsFor(cmd);
		OptionValues opts;
		for (const QString &name : defaults.checked)
			opts.insert(name, defaults.values.value(name));
		for (auto it = userOptions.constBegin(); it != userOptions.constEnd(); ++it) {
			if (!spec.options.contains(it.key())) {
				if (!warnedOptions.contains(cmd + it.key())) {
					warnedOptions.insert(cmd + it.key());
					err("namdhc: warning: --" + findOption(it.key())->cliName + " does not apply to " + cmd + ", ignored\n");
				}
				continue;
			}
			opts.insert(it.key(), it.value());
		}
		if (noSubdir)
			opts.remove("createSubDir");

		QStringList outputs;
		for (const QString &w : wantedOutputs)
			if (spec.outputExts.contains(w))
				outputs << w;
		if (outputs.isEmpty() && !spec.outputExts.isEmpty())
			outputs << spec.outputExts.first();

		BuildRequest req;
		req.cmd = cmd;
		req.options = opts;
		req.outputExts = outputs;
		req.inputExts = spec.inputExts;
		req.files = {file};
		req.outputFolder = outputFolder;
		req.useInputFolder = outputFolder.isEmpty();
		buildJobs(req, queue, [&](const Job &newJob, const Job &, int) {
			if (skipDuplicates) {
				err("namdhc: skipping '" + newJob.fromFile + "': output " + newJob.toFile + " already queued\n");
				return DuplicateAction::Skip;
			}
			return DuplicateAction::Rename;
		});
	}
	for (int i = 0; i < queue.size(); ++i)
		queue[i].idx = i + 1;

	if (queue.isEmpty()) {
		err("namdhc: nothing to do\n");
		return 1;
	}

	if (parser.isSet(optDryRun)) {
		for (const Job &j : std::as_const(queue)) {
			QStringList args{chdman, j.cmd};
			args << j.cmdArgs << "-i" << j.fromFileFull;
			if (!j.toFileFull.isEmpty())
				args << "-o" << j.toFileFull;
			for (QString &a : args)
				if (a.contains(' ') || a.contains('\''))
					a = "'" + a.replace("'", "'\\''") + "'";
			out(QString("[%1/%2] %3\n").arg(j.idx).arg(queue.size()).arg(args.join(' ')));
		}
		return 0;
	}

	// ---- run
	int jobs = settings.value(keys::JobQueueSize, DefaultJobQueueSize).toInt();
	if (parser.isSet(optJobs))
		jobs = parser.value(optJobs).toInt();
	jobs = qBound(1, jobs, 64);

	RunnerConfig cfg;
	cfg.chdman = chdman;
	cfg.sevenZip = sevenZip;
	cfg.tempDir = QDir::tempPath() + "/namDHC-" + QString::number(QCoreApplication::applicationPid());

	JobQueue jq(cfg);
	LiveView view(tty, color, quiet);
	view.setSlots(qMin(jobs, int(queue.size())));

	const QString finishedText = commandSpec(queue.first().cmd).finishedText;
	int exitCode = 0;
	QString failedReports;

	QObject::connect(&jq, &JobQueue::slotProgress, &view, [&](int slot, int pct, const QString &text, bool busy) {
		view.setSlot(slot, pct, text, busy);
	});
	QObject::connect(&jq, &JobQueue::overallProgress, &view, [&](int pct, const QString &text) { view.setOverall(pct, text); });
	if (verbose)
		QObject::connect(&jq, &JobQueue::logMessage, &view, [&](const QString &msg) { view.print(msg); });
	QObject::connect(&jq, &JobQueue::jobFinished, &view, [&](int slot, const Job &job, JobStatus st) {
		const JobRunner *r = jq.slotRunner(slot);
		const QString counter = QString("[%1/%2]").arg(jq.tally().finished + 1).arg(jq.tally().total);
		QString tag, detail;
		switch (st) {
		case JobStatus::Success:
			tag = view.paint(" OK  ", "1;32");
			if (job.cmd.startsWith("create") && r && r->fileStartSize() > 0)
				detail = QString("  (%1 -> %2, -%3%)")
					.arg(formatBytesIEC(r->fileStartSize()), formatBytesIEC(r->fileFinishSize()))
					.arg(qRound((1.0 - double(r->fileFinishSize()) / double(r->fileStartSize())) * 100.0));
			break;
		case JobStatus::FileExists:
			tag = view.paint("SKIP ", "1;33");
			detail = "  (output exists, use --force)";
			break;
		case JobStatus::Cancelled:
			tag = view.paint("STOP ", "1;33");
			break;
		default:
			tag = view.paint("FAIL ", "1;31");
			detail = "  " + (r ? r->resultMessage() : QString());
			if (r)
				failedReports += r->report();
			break;
		}
		if (!quiet || st == JobStatus::Error)
			view.print(tag + counter + " " + job.workingTitle + detail);
		view.setSlot(slot, 0, QString(), false);
	});

	QObject::connect(&jq, &JobQueue::allFinished, &app, [&] {
		view.finish();
		const Tally &t = jq.tally();
		QString summary = jq.summaryText(finishedText);
		if (queue.first().cmd.startsWith("create") && !jq.sizeSavingsText().isEmpty())
			summary += "\n" + jq.sizeSavingsText();

		// Failed jobs always show their report so the cause is visible without --verbose.
		if (verbose)
			out(jq.allReport().trimmed() + "\n\n");
		else if (!failedReports.isEmpty())
			out(failedReports.trimmed() + "\n\n");
		out("\n" + summary + "\n");

		if (parser.isSet(optReport)) {
			QFile f(parser.value(optReport));
			if (f.open(QIODevice::WriteOnly | QIODevice::Text))
				f.write((summary + "\n" + jq.allReport() + "\n").toUtf8());
			else
				err("namdhc: unable to write report to " + parser.value(optReport) + "\n");
		}
		removeDirRecursively(cfg.tempDir);
		exitCode = t.withError ? 1 : (t.cancelled ? 130 : 0);
		QTimer::singleShot(0, &app, [&] { app.exit(exitCode); });
	});

	// Ctrl+C: cancel everything (partial files are cleaned up); a third press exits at once.
	if (::pipe(g_sigPipe) == 0) {
		auto *notifier = new QSocketNotifier(g_sigPipe[0], QSocketNotifier::Read, &app);
		QObject::connect(notifier, &QSocketNotifier::activated, &app, [&, presses = 0]() mutable {
			char buf[16];
			[[maybe_unused]] auto r = ::read(g_sigPipe[0], buf, sizeof(buf));
			if (++presses >= 3)
				_exit(130);
			view.print(view.paint("Cancelling all jobs... (press Ctrl+C twice more to quit immediately)", "1;33"));
			jq.cancelAll();
		});
		std::signal(SIGINT, onSignal);
		std::signal(SIGTERM, onSignal);
	}

	if (!quiet)
		view.print(QString("%1 %2 job%3, %4 at a time").arg(queue.size()).arg(command).arg(queue.size() == 1 ? "" : "s").arg(jobs));
	jq.start(queue, jobs);
	return app.exec();
}
