# Build and installation

[Back to README](../README.md)

## Requirements

- CrowPanel Advance ESP32-P4 with a **1024×600** display and speaker.
- A FAT32-formatted microSD card containing the supplied animation assets.
- ESP-IDF **5.5.2**, target **`esp32p4`**.
- LVGL **8.4.0** and `esp_lvgl_port` **2.7.2**, as recorded in [`dependencies.lock`](../dependencies.lock).
- The supplied [`sdkconfig.defaults`](../sdkconfig.defaults) selects 16 MB flash, PSRAM, RGB565, and the 1024×600 display profile. Use a board and display matching this configuration.

The application displays a blocking warning if the microSD card cannot be mounted. Insert the prepared card and reboot the panel.

## Prepare the microSD card

Copy the **contents** of [`assets_sdcard/`](../assets_sdcard/) into an `assets/` directory at the root of the card:

```text
<SD root>/assets/
├── book_open.cfa
├── book_swipe.cfa
├── book_close.cfa
├── bird_idle.gif
├── bird_start_talk.gif
└── bird_talk.gif
```

Keep these filenames unchanged. The default configuration uses CFA for the book and GIF for the bird. The alternative book-GIF and bird-CFA configurations require additional assets that are not supplied in `assets_sdcard/`.

## Build and install

Install ESP-IDF 5.5.2 and Git LFS, then clone this branch with its large assets:

```bash
git lfs install
git clone --branch source-code https://github.com/AI-devices/FairyTales.git
cd FairyTales
git lfs pull
```

Git LFS tracks source MOV clips, source TTF fonts, and runtime CFA animations via [`.gitattributes`](../.gitattributes). Check that these files contain media rather than LFS pointer text. ZIP downloads may contain pointers instead of the required assets.

In an activated ESP-IDF 5.5.2 environment, build the project:

```bash
idf.py build
```

A fresh checkout uses `sdkconfig.defaults`. An existing ignored `sdkconfig` can override these defaults; review it before preparing a release.

Connect the panel's **UART0 USB-C port**, then flash the firmware and open the serial monitor:

```bash
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

The build includes a SPIFFS image generated from `spiffs_root/`, and the project flash target includes it. Flash the matching application, partition table, bootloader, and SPIFFS image as one set.

### Separate flashing package

The ready-to-use Windows flashing package is available in the [main branch](https://github.com/AI-devices/FairyTales/tree/main). The flashing utility and prebuilt firmware are not included in this source-code branch; use the steps above to build and flash your own firmware.

The current [`partitions.csv`](../partitions.csv) places the application at `0x10000` and SPIFFS at `0x290000`. Use the generated flash arguments for the images from your build.

