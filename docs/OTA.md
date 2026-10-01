# OTA updates and release publishing

## How device updates work

The updater reads `ota.json` from the latest public GitHub Release for this repository. The manifest identifies the release firmware image, semantic version, byte size, and SHA-256 digest. The device downloads the image over verified HTTPS, accepts links only from this repository's GitHub release downloads, and checks the complete size and digest before completing the update. The ESP32 then boots the new image from the inactive OTA application slot.

The updater is available in firmware version `0.1.0` and later. The first installation must be made by USB/serial with the full-flash image because an older installed image does not contain the OTA interface. Once running an OTA-capable build, choose **Settings → Firmware Updates → Check for Updates**. If a newer release is found, press the button again to install it. The device restarts after a successful install.

To test the update path from the initial OTA release, install the `v0.1.0` full-flash image, connect the device to Wi-Fi, then check for updates. Release `v0.1.1` adds the color test page and should be offered to the device.

The device needs Wi-Fi access to GitHub and a valid clock for HTTPS certificate checks. Credentials and the configured SpoolmanSync address are stored in NVS and are retained by an OTA update. A full serial flash can erase that configuration.

## Publish a release

1. Update `SPOOLMAN_CYD_VERSION` in `include/FirmwareVersion.h` to the new `MAJOR.MINOR.PATCH` version.
2. Commit the firmware and documentation changes.
3. Create and push a matching tag, for example `v0.1.1`:

   ```sh
   git tag v0.1.1
   git push origin v0.1.1
   ```

4. The GitHub Actions workflow builds the `cyd` environment and creates a GitHub Release containing:
   - `spoolman-cyd-vX.Y.Z.bin`: application-only image used for OTA;
   - `spoolman-cyd-vX.Y.Z-full.bin`: complete image for first-time serial flashing;
   - `ota.json`: manifest consumed by devices.

The workflow checks that the pushed tag version matches `SPOOLMAN_CYD_VERSION`; a mismatch fails the release build. GitHub Actions must be enabled and the workflow's default `GITHUB_TOKEN` must have **Contents: read and write** permission, which is set in the workflow file.

The manifest is attached to each release and its version-specific firmware URL remains stable. The device discovers it through GitHub's `/releases/latest/download/ota.json` URL. Only publish stable numeric semantic versions; prereleases are not selected by the latest-release endpoint.

## Manual build and first flash image

Build the application with:

```sh
pio run -e cyd
```

Use `.pio/build/cyd/firmware.bin` for OTA. For first serial installation, merge the bootloader, partition table, Arduino OTA boot app, and firmware image at their ESP32 flash offsets:

```sh
python3 "$HOME/.platformio/packages/tool-esptoolpy/esptool.py" --chip esp32 merge_bin \
  --output spoolman-cyd-full.bin --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x1000 .pio/build/cyd/bootloader.bin \
  0x8000 .pio/build/cyd/partitions.bin \
  0xe000 "$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" \
  0x10000 .pio/build/cyd/firmware.bin
```

Flash the merged image at `0x0`. The configured partition table has two OTA app partitions, each 1,310,720 bytes. Keep the compiled application smaller than this partition size for OTA to work.

## Recovery

If the device cannot boot after an update, first power-cycle it once; ESP32 OTA metadata keeps the previously valid app available if the new app fails validation. If it still does not start, use a serial flasher with the release `-full.bin` image at `0x0`. This restores a known release but erases saved device settings.
