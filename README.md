# Bash Screensavers

![Logo](spotlight/logos/logo.320x160.png)

Welcome to **Bash Screensavers**, a collection of animated ASCII art for your terminal. This project brings classic screensaver fun to the command line, starting in pure `bash` and now featuring **high-performance C native engines** for the most popular visualizers.

This fork builds upon the original by introducing **native C optimizations**, new visualizers, and enhanced features to ensure a seamless experience on macOS and Linux.

[Key Features](#key-features) -
[Gallery](#gallery) -
[Quickstart](#quickstart) -
[Contributing](#contributing) -
[Spotlight](#spotlight)

[![Release](https://img.shields.io/github/v/release/attogram/bash-screensavers?style=flat)](https://github.com/attogram/bash-screensavers/releases)
[![License](https://img.shields.io/github/license/attogram/bash-screensavers?style=flat)](./LICENSE)
![Bash ≥3.2](https://img.shields.io/badge/bash-%3E=3.2-blue?style=flat)

## Key Features

*   **Smart Native Engine:** The most popular screensavers (`rorschach-led`, `matrix`, `dunes`, `perlin-ascii`, `perlin-pixel`) are now rewritten in **C** for ultra-low CPU usage (< 1%) and buttery smooth **60 FPS** animations.
*   **Automatic Compilation:** The main launcher handles C compilation automatically. If a compiler (`clang`, `gcc`, or `cc`) is present, it builds the native binary on the first run.
*   **Pure Bash Fallback:** No compiler? No problem. The project remains fully functional with the original pure `bash` implementations as a fallback.
*   **macOS Sleep Prevention:** Automatically uses `caffeinate -d` on macOS to prevent the display from sleeping while the screensaver is active.
*   **Classic & Modern Visuals:** From the mesmerizing **Dunes** (3D Perlin noise) to **Rorschach** inkblots (symmetrical generative art).

## Gallery

The [Gallery README](./gallery/README.md) has details on all available screensavers.

[![Dunes](gallery/dunes/dunes.gif)](./gallery/README.md)

## Quickstart

1.  **Clone the repository:**
    ```bash
    git clone https://github.com/attogram/bash-screensavers.git
    cd bash-screensavers
    ```

2.  **Run the main script:**
    ```bash
    ./screensaver.sh
    ```
    This will display a menu where you can choose a screensaver.

## Command-Line Usage

You can also run screensavers directly from the command line.

| Command                           | Description                            |
| --------------------------------- | -------------------------------------- |
| `./screensaver.sh`                | Show the interactive menu.             |
| `./screensaver.sh <name>`         | Run a specific screensaver by name.    |
| `./screensaver.sh <number>`       | Run a specific screensaver by number.  |
| `./screensaver.sh -r`             | Start a random screensaver.            |
| `./screensaver.sh -h`             | Display the help message.              |
| `./screensaver.sh -v`             | Show the current version.              |
| `./gallery/<name>/<name>.sh`      | Run a screensaver script directly.     |

## Contributing

Contributions are welcome! If you have an idea for a new screensaver or an improvement, please see [CONTRIBUTING.md](./CONTRIBUTING.md). AI assistants and creative coders are encouraged to participate.

## Project Structure

*   **[Gallery](./gallery/README.md):** Contains all the screensaver scripts.
*   **[Jury](./jury/README.md):** The testing suite that ensures everything works as expected.
*   **[Library](./library/README.md):** Supporting scripts and functions.
*   **[Spotlight](./spotlight/README.md):** Tools for creating previews and marketing materials.

---

*Made with ❤️ and a lot of bash.*
