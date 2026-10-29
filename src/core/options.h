// SPDX-License-Identifier: GPL-3.0-or-later
// CHDMAN option and command definitions shared by the CLI and the GUI.
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace namdhc {

enum class OptKind { Flag, Edit, Dropdown };

struct ChdmanOption {
	QString name;          // internal id ("force", "hunksize", ...)
	QString param;         // chdman short switch without the dash ("f", "hs"); empty for file options
	QString cliName;       // long name used by the namdhc CLI ("force", "hunk-size", ...)
	QString description;   // label shown in the GUI
	QString helpText;      // tooltip / CLI help
	OptKind kind = OptKind::Flag;
	QString defaultValue;  // default edit value
	QStringList dropdownLabels;
	QStringList dropdownValues;
	bool isFileOption = false;  // handled by namdhc, not passed to chdman
	QString masterOf;           // option that is only enabled while this one is checked
	int indent = 0;
	bool hidden = false;
};

// Checked options and their values. Presence of a key means the option is enabled.
using OptionValues = QHash<QString, QString>;

struct CommandSpec {
	QString cmd;              // chdman command ("createcd", "verify", ...)
	QStringList inputExts;    // accepted input extensions (archives included)
	QStringList outputExts;   // possible output extensions
	QStringList options;      // option names available for this command, in display order
	QString description;      // "Create CHD from a CD image"
	QString finishedText;     // "Jobs created"
};

const QList<ChdmanOption> &allOptions();
const ChdmanOption *findOption(const QString &name);
const ChdmanOption *findOptionByCli(const QString &cliName);

// Job types as shown in the GUI job dropdown.
enum class JobType { Create, Extract, Info, Verify };
QString jobTypeLabel(JobType t);
QString jobTypeKey(JobType t);  // "create", "extract", "info", "verify"

// Media types ("cd", "dvd", "hd", "ld", "raw").
QStringList mediaKeys();
QString mediaLabel(const QString &media);

QString commandFor(JobType t, const QString &media);
CommandSpec commandSpec(const QString &cmd);
bool isKnownCommand(const QString &cmd);

// Per-command defaults (edit values and pre-checked options), mirroring namDHC.
struct OptionDefaults {
	QHash<QString, QString> values;
	QStringList checked;
};
OptionDefaults defaultsFor(const QString &cmd);

// Hard disk templates offered for the -tp option.
QStringList hdTemplateLabels();

bool isArchiveExt(const QString &ext);
bool isMultiFileExtractType(const QString &ext);  // cue/toc/gdi

} // namespace namdhc
