# CrowPanel P4 Fairy-Tale Storybook

Interactive branching storybook for **Elecrow CrowPanel Advanced ESP32-P4 panels with 1024 × 600 touch displays**. The application combines illustrated scenes, animations, built-in narration audio, and touch-based choices.

## Requirements

- CrowPanel Advance ESP32-P4 with a **1024×600 display**, 16 MB flash, PSRAM, and a speaker.
- FAT32 microSD card with the contents of [`assets_sdcard/`](./assets_sdcard/) copied into `<SD root>/assets/`.
- **ESP-IDF 5.5.2** for building from source; the project uses **LVGL 8.4.0**.

## Documentation

- [Using the Storybook](./README_STORYBOOK.md) — modes, touch controls, and settings, with screenshots.
- [Build and installation](./docs/BUILD.md) — source download, Git LFS, microSD preparation, and flashing.
- [Project structure and media](./docs/PROJECT.md) — components, assets, and story customization.

## Flashing package

The ready-to-use Windows flashing package and microSD assets are available in the [main branch](https://github.com/AI-devices/FairyTales/tree/main).
