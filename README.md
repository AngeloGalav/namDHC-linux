# namDHC for Linux

Native Linux version of [namDHC](https://github.com/umageddon/namDHC), the frontend for MAME's
**chdman**. It comes with:

- **namdhc-gui**: Qt6 graphical interface with the same workflow as namDHC on Windows
- **namdhc**: command line tool for batch jobs, e.g. `namdhc --folder .`
- **chdman**: a native build of MAME's chdman, bundled with the project

## Install

```sh
# Fedora
sudo dnf install cmake gcc-c++ qt6-qtbase-devel
# Debian / Ubuntu
sudo apt install cmake g++ qt6-base-dev
# Arch
sudo pacman -S --needed cmake gcc qt6-base

./install.sh              # installs into ~/.local (no root needed)
./install.sh --system     # installs into /usr/local
./install.sh --uninstall  # removes it again (add --system if you installed with it)
```

`namdhc`, `namdhc-gui` and `chdman` are installed into `<prefix>/bin`, and namDHC is added to the
applications menu. Optional tools: install **7-Zip** (`7z`/`7zz`) for `.7z` input, and **unzip**
or 7-Zip for `.zip` input.

To build by hand: `cmake -B build && cmake --build build -j`. Pass `-DNAMDHC_BUILD_GUI=OFF` to build
only the command line tools, which need just Qt6 Core.

## Command line

```
namdhc [COMMAND] [MEDIA] [OPTIONS] [FILES or FOLDERS...]
```

| Example | What it does |
|---|---|
| `namdhc --folder .` | converts every `.cue/.gdi/.toc/.iso` (and `.zip/.7z` containing one) in the folder and its subfolders to `.chd`, saved next to each source |
| `namdhc create dvd -o ~/chd *.iso` | creates DVD CHDs (PS2, PSP, ...) in `~/chd` |
| `namdhc extract cd -t gdi game.chd` | extracts a Dreamcast CHD back to GDI |
| `namdhc extract ~/chd` | extracts every CHD, choosing CD/DVD/HD from the CHD metadata |
| `namdhc verify -j 4 ~/roms` | verifies all CHDs, 4 at a time |
| `namdhc info game.chd` | shows information about a CHD |
| `namdhc --dry-run --folder .` | shows the chdman commands that would run, without running them |
| `namdhc chdman listtemplates` | runs the bundled chdman directly |

With media `auto` (the default), `create` sends `.cue/.gdi/.toc` files to CD, `.iso` files up to
900 MiB to CD and larger ones to DVD. Pass `cd` or `dvd` to force a media type.

Every chdman option is available as a long flag (`--compression`, `--hunk-size`,
`--num-processors`, `--output-parent`, `--split-bin`, ...). The namDHC file options are flags too:
`--force`, `--subdir`, `--delete-input`, `--delete-input-dir`, `--keep-incomplete`. Run
`namdhc --help` for the full list.

Ctrl+C cancels the running jobs and removes their partial output. The exit code is 0 when every job
succeeds, 1 when at least one fails, 130 when the run is cancelled and 2 for usage errors.

## Graphical interface

Choose a job type and a media type, add files or folders (drag and drop also works), choose an
output folder and press start. It provides the same features as namDHC:

- jobs that run at the same time, each with its own progress bar and cancel button
- per-job output folders, deleting the input files after a successful job, keeping or removing
  incomplete output
- zip/7z input
- a CHD info viewer
- a verbose log window and an optional live chdman console for each job
- a final report with the space saved
- a finished sound

Settings are stored in `~/.config/namDHC/namDHC.conf`. The command line tool reads the same
"jobs to run concurrently" value.

## AI disclosure

The Linux port (the CLI, the Qt6 GUI, the build system and `install.sh`) was written with
the help of [Claude Code](https://claude.com/claude-code), Anthropic's AI coding assistant,
under the direction of the maintainer. The design and features follow the original namDHC.
chdman and the vendored code in `lib/mame` are upstream MAME sources and were not written
by AI; the only local change is the small patch documented in `lib/mame/VERSION.txt`.

## License

GPL-3.0-or-later. chdman and the vendored MAME/3rdparty code keep their own licenses
(BSD-3-Clause, zlib, public domain, MIT; see the license files in `lib/mame`).
