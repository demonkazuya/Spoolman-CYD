# Spoolman CYD

Spoolman CYD is firmware for the ESP32-2432S028 (Cheap Yellow Display) that browses SpoolmanSync printers, AMS trays, external spool slots, and spool inventory. It can assign, replace, and clear tray assignments, and supports portrait/landscape layouts, green and charcoal light/dark themes, and a labeled LCD color test with red/blue swap and inversion controls.

## Install

For a first installation, download the `spoolman-cyd-<version>-full.bin` asset from the [Releases](https://github.com/demonkazuya/Spoolman-CYD/releases) page and flash it at address `0x0` with an ESP32 flasher. The full image includes the bootloader, partition table, OTA data partition, and application. Back up any device configuration first: a full flash erases stored Wi-Fi and server settings.

After boot, connect the device to Wi-Fi and enter the SpoolmanSync server IP and port in setup. It verifies the API before saving the address. Configure the same server address and port that your browser can reach from the local network.

## Updates

After installing a release that supports OTA, open **Settings → Firmware Updates** to check GitHub and install a newer release. Keep the device on stable Wi-Fi and power while the update downloads. The updater verifies the release manifest, firmware size, and SHA-256 before switching to the updated image. If installation fails, the current firmware remains available through the ESP32 OTA slot mechanism; recovery by USB serial flashing is documented in [OTA updates](docs/OTA.md).

## Build

Install PlatformIO Core, then run:

```sh
pio run -e cyd
```

The application image is `.pio/build/cyd/firmware.bin`. See [OTA updates](docs/OTA.md) for release publishing and image formats. See [hardware notes](docs/HARDWARE.md) for board and display configuration, and [API notes](docs/API.md) for the verified SpoolmanSync routes.
