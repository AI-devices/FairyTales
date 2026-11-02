# Project structure and media

[Back to README](../README.md)

## Repository structure

| Path | Purpose |
| --- | --- |
| `main/` | Startup, storage mounting, display configuration, project Kconfig |
| `components/fairytale_runtime/` | Runtime services, saved settings, narration preload |
| `components/ui/` | LVGL screens, story coordinator/engine, text, media manifest |
| `components/audio_session/`, `components/audio_player_p4/` | Local audio decoding, playback, and sound mixing |
| `components/media_pipeline/` | Media storage, loading, caching, GIF/CFA decoding |
| `components/settings_store/`, `components/ui_dispatch/` | Persistent settings and UI event dispatch |
| `components/espressif__esp32_p4_function_ev_board/`, `components/bsp_extra/` | Board support and extensions |
| `spiffs_root/assets/` | Firmware-bundled story images, decorative UI assets, narration, and sound effects |
| `assets_sdcard/` | Supplied runtime book and bird animations |
| `assets_src/` | Editable MOV, PNG, and TTF source assets |
| `python/` | Offline media preparation utilities |

## Media and customization

Small decorative assets and story illustrations are loaded from SPIFFS. Narration MP3 files, WAV sound effects, and the scene-A PCM clip also reside in SPIFFS. Large book CFA files and bird GIFs are loaded from microSD; media caches use PSRAM where configured.

The story coordinator sequences recorded audio, sound effects, and transitions. Editing built-in text alone does **not** change the spoken narration. To replace a story, update the corresponding texts, illustrations, audio recordings, and media references together.

- [`python/compile_mov_to_cfa.py`](../python/compile_mov_to_cfa.py) prepares book animations from `assets_src/anim_mov/`. It requires FFmpeg/FFprobe and includes its own LZ4 block encoder. Its default output directories are `.compile_mov_to_cfa/` and `sdcard_root/`; it also updates `assets_sdcard/` unless disabled with `--no-assets-copy`.
- [`python/stage_narration_audio.py`](../python/stage_narration_audio.py) normalizes the nine narrator recordings using FFmpeg. Provide recordings with `--input <directory>`; the default `assets_src/audio_narration/` directory is not included. The default output is `spiffs_root/assets/audio/`. Character voices, sound effects, and the PCM clip are separate assets.

The shipped runtime assets are already prepared; these tools are not required for a normal build.

## Story flow details

Story Loop repeats the opening scene. Full Story follows the narrated route `A → B1 → B1_1 → B1_1_1 → D1`; alternate choices are disabled.

On the first story entry, tap **Got it** to close the mode hint and start playback. **Don't show it again** saves the hint preference across reboots. The selected story mode is not persisted; a device reboot starts in Story Loop.

Opening Settings stops the current audio and suspends the story timers. Closing Settings restarts narration from the beginning of the interrupted intro, scene, or outro, and rearms choice or automatic-restart timers. Volume and brightness are saved across reboots.
