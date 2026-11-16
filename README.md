# PCManFM-PS4

A native **PlayStation 4 (Orbis OS)** port of the lightweight [PCManFM](https://github.com/lxde/pcmanfm) file manager, built as a standalone `.pkg` home-screen application using **OpenOrbis `v0.5.4`**, **SDL2 (`bucanero/SDL-PS4`)**, and **Dear ImGui**.

## Features
- **PCManFM Layout**: Places & Bookmarks sidebar, expandable Directory Tree, Tabbed browsing, and **Dual-Pane mode**.
- **3 View Modes**: Detailed List (sortable Name, Size, Type, Mode, Modified), Compact Multi-Column, and Icon Grid.
- **Full File Operations**: Background threaded Copy, Move (Cut & Paste), Recursive Delete, Rename, and New Folder (`mkdir`) using `OrbisPthread` + `64 KB` heap I/O buffers.
- **Native PS4 Keyboard**: Uses `SceImeDialog` for Rename, New Folder, Search Filter, and Go-to-Path (with an automatic Dear ImGui fallback modal).
- **Built-in Viewers**: Text / Log viewer (up to 64 KB) and Hex viewer (16-byte offset + ASCII) + File Properties.
- **Verbose FTP Debugging**: Writes timestamped logs to `/data/pcmanfm/log.txt` with immediate `fflush()`/`fsync()` on every step.

## DualShock 4 Controls
| Button | Action |
|---|---|
| **D-Pad / Left Stick** | Navigate files & UI |
| **Cross (`X`)** | Open folder / Preview file |
| **Circle (`O`)** | Go up to parent directory / Close modal |
| **Square (`[]`)** | Toggle multi-file selection |
| **Triangle (`/\`)** | Open Actions menu (Copy, Cut, Paste, Rename, New Folder, Delete, Properties) |
| **L1 / R1** | Switch tabs |
| **Share / Select** | Toggle hidden files |
| **Options** | Open Preferences |

## Building (GitHub Actions CI)
Push to GitHub and download `PCManFM-PS4-v01.01.pkg` directly from the **Build PS4 PKG** workflow artifacts (no `.zip` wrapper).

## Installing & Debugging on PS4 (FW 13.52 + GoldHEN)
1. Upload `PCManFM-PS4-v01.01.pkg` to `/data/pkg/` via GoldHEN FTP (`:2121`) or copy to a USB drive.
2. Install via **GoldHEN → Package Installer**.
3. Check `/data/pcmanfm/log.txt` via FTP (`:2121`) or GoldHEN `klog` (`:3232`) for full startup and runtime trace logs.
