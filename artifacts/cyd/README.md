# CYD firmware images

`esp32-2432s028-full.bin` is the latest locally built merged image. Flash it at offset `0x0` for first-time installation. It contains bootloader, partition table, OTA boot data, and the application; a full flash may erase saved settings.

The OTA application image is `.pio/build/cyd/firmware.bin`. It is written to the inactive application slot by the firmware updater. Do not flash this app-only image at `0x0` with a serial flasher.

Build with PlatformIO:

```sh
pio run -e cyd
```

The `cyd` environment uses two 1,310,720-byte OTA application slots. The current application fits those slots. To reproduce the merged image, use the esptool package installed by PlatformIO:

```sh
python3 "$HOME/.platformio/packages/tool-esptoolpy/esptool.py" --chip esp32 merge_bin \
  --output artifacts/cyd/esp32-2432s028-full.bin \
  --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x1000 .pio/build/cyd/bootloader.bin \
  0x8000 .pio/build/cyd/partitions.bin \
  0xe000 "$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" \
  0x10000 .pio/build/cyd/firmware.bin
```

Public versioned downloads are attached to [GitHub Releases](https://github.com/demonkazuya/Spoolman-CYD/releases). See [OTA updates](../../docs/OTA.md) for first install and release publishing.
