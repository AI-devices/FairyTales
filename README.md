# CrowPanel P4 Fairy-Tale Storybook

Interactive branching storybook for **Elecrow CrowPanel Advanced ESP32-P4 panels with 1024 × 600 touch displays**. The application combines illustrated scenes, animations, built-in narration audio, and touch-based choices.

This package provides a ready-to-use Windows flashing tool for hardware revision **V1.2**.

## Prepare the microSD Card

A **microSD card is required** for the application.

1. Format the microSD card as **FAT32**.
   - On Windows, right-click the card in File Explorer, select **Format**, and choose **FAT32**.
   - If FAT32 is not available in the standard formatting dialog, use a FAT32 formatting utility.
2. Copy the complete `assets` folder from `sd_card/` to the **root of the microSD card**.
3. Insert the prepared card into the panel's **TF card slot**.

The resulting card structure must be:

```text
assets/
├── book_open.cfa
├── book_swipe.cfa
├── book_close.cfa
├── bird_idle.gif
├── bird_start_talk.gif
└── bird_talk.gif
```

## Speakers

Two speakers are required.

Connect one speaker to the **SPK-L** connector and the other speaker to the **SPK-R** connector.

## Flashing

1. Install the **CH341 USB-to-Serial driver for Windows**:
   [Download CH341SER driver](https://www.wch-ic.com/downloads/CH341SER_ZIP.html)
2. Download and fully extract `CrowPanelv1.2_StoryBookP4_SPIFFS_v1.1.zip`.
3. Make sure the microSD card is formatted as **FAT32**, contains the required `assets` folder, and is inserted into the panel.
4. Connect the panel’s **UART0 USB-C port** to a Windows computer using a data-capable USB cable.
5. Open the extracted `CrowPanel_P4_SPIFFS_flasher` folder.
6. Run `CrowPanel_P4_SPIFFS_flasher.exe`.
7. Select the panel's COM port. Click **Refresh** if the port is not listed.
8. Make sure **Clean install (erase entire flash)** is enabled.
9. Click **Flash panel**.
10. Do not disconnect the panel while flashing.
11. Wait for the **Firmware installed successfully** message.

The panel restarts automatically after flashing.

## USB-C Ports and Power

CrowPanel Advanced ESP32-P4 has two USB-C ports:

- **UART0** — used for firmware flashing and serial communication.
- **USB 2.0** — native USB port that can also provide additional 5V power.

Use **UART0** as the main connection for flashing.

If the panel becomes unstable under load — for example, the display goes black, the panel resets, or audio operation becomes unstable — connect a second USB-C cable to **USB 2.0** for additional power.

If the panel works reliably with one cable, the second cable is not required.

## Troubleshooting

If the panel is not detected, reconnect it through **UART0**, close any application using the COM port, and click **Refresh**.

If the application does not start correctly, make sure:

- the microSD card is inserted and formatted as **FAT32**;
- all required files are located under `<SD root>/assets/`;
- both speakers are connected to the **SPK-L** and **SPK-R** connectors.

On the **first startup with the microSD card**, the card may occasionally not be detected. If this happens, **restart the panel**. The microSD card should be detected normally after the restart.
