// SPDX-License-Identifier: GPL-3.0-or-later
// Settings shared by the GUI and the CLI (~/.config/namDHC/namDHC.conf).
#pragma once

#include <QSettings>

namespace namdhc {

inline QSettings appSettings()
{
	return QSettings(QSettings::IniFormat, QSettings::UserScope, "namDHC", "namDHC");
}

namespace keys {
inline constexpr const char *JobQueueSize = "Settings/JOB_QUEUE_SIZE";
inline constexpr const char *OutputFolder = "Settings/OUTPUT_FOLDER";
inline constexpr const char *ShowJobConsole = "Settings/SHOW_JOB_CONSOLE";
inline constexpr const char *ShowVerboseWindow = "Settings/SHOW_VERBOSE_WINDOW";
inline constexpr const char *PlaySoundFinished = "Settings/PLAY_SONG_FINISHED";
inline constexpr const char *RemoveFileEntryAfterFinish = "Settings/REMOVE_FILE_ENTRY_AFTER_FINISH";
inline constexpr const char *NoDpiScale = "Settings/APP_NO_DPI_SCALE";
inline constexpr const char *UseInputFolderAsOutput = "Settings/USE_INPUTFOLDER_AS_OUTPUT";
inline constexpr const char *CheckForUpdatesStartup = "Settings/CHECK_FOR_UPDATES_STARTUP";
inline constexpr const char *LastInputBrowseFolder = "Settings/LAST_INPUT_BROWSE_FOLDER";
inline constexpr const char *LastOutputBrowseFolder = "Settings/LAST_OUTPUT_BROWSE_FOLDER";
inline constexpr const char *SevenZipCustomPath = "Settings/SEVENZIP_CUSTOM_PATH";
inline constexpr const char *ChdmanCustomPath = "Settings/CHDMAN_CUSTOM_PATH";
inline constexpr const char *MainWindowGeometry = "Window/MAIN_GEOMETRY";
inline constexpr const char *VerboseWindowGeometry = "Window/VERBOSE_GEOMETRY";
} // namespace keys

inline constexpr int DefaultJobQueueSize = 3;
inline constexpr int JobQueueSizeLimit = 10;

} // namespace namdhc
