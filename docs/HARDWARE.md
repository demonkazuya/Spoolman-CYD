# CYD hardware bring-up

The board is marked `ESP32-2432S028`. The initial bring-up configuration targets the standard 2.8-inch resistive-touch CYD variant described by the [witnessmenow CYD reference project](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display): ILI9341 display and XPT2046 touch controller. The board reference documents the display and touch pin maps in [`PINS.md`](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/blob/main/PINS.md), and its [touch test](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/tree/main/Examples/Basics/2-TouchTest) provides the TFT_eSPI setup used here.

## Display

| Signal | ESP32 GPIO |
| --- | ---: |
| MISO | 12 |
| MOSI | 13 |
| SCLK | 14 |
| CS | 15 |
| DC | 2 |
| Reset | Not connected (`-1`) |
| Backlight | 21 |

The display uses the HSPI pin set. The bring-up sketch sets portrait orientation (240 × 320).

## Resistive touch

| Signal | ESP32 GPIO |
| --- | ---: |
| IRQ | 36 |
| MOSI | 32 |
| MISO | 39 |
| SCLK | 25 |
| CS | 33 |

Touch uses a separate SPI pin set. This first sketch reports raw touch coordinates and pressure; it does not yet calibrate touch positions to UI coordinates.

## Bring-up

Build with PlatformIO using the `cyd` environment. The current firmware uses a Wi-Fi-first setup flow. When Wi-Fi is not configured, setup presents a Scan Wi-Fi Networks button that opens a dedicated scan and selection page; after the device connects, it asks for the SpoolmanSync server IP and port. On later boots with saved Wi-Fi credentials, an initializing screen remains visible while the device reconnects, then opens the printer selection list when Wi-Fi is available. The printer list is the main screen, with a refresh icon beside the gear in its header to retry loading after a server outage; the gear opens Settings. Settings shows the device IP and saved SpoolmanSync address, has orientation, Wi-Fi scan, server, theme, color test, and firmware update buttons, and opens the dedicated Wi-Fi scan and selection page. The network page lets the user choose an SSID and opens an on-screen password keyboard. Wi-Fi credentials are saved in ESP32 NVS only after a successful connection and reused after reboot. SpoolmanSync server settings accept an IPv4 address and port, test them against the verified `GET /api/printers` route, and save them in NVS only after the API check succeeds. There is no default server address: the server must be configured and verified before printer data can load. A green and charcoal theme defaults to dark mode, with a Settings control to switch between light and dark; the choice is stored in NVS. The Color Test page shows labeled reference swatches and controls to swap red/blue channels or toggle LCD inversion; both calibration choices are stored in NVS and restored after reboot. Theme and display settings report NVS load/save failures and log the restored values to Serial. A display orientation control switches between portrait and landscape, stores the choice, and restarts the device so the display and touch drivers reinitialize together. The Printers screen loads the verified route and supports printer → AMS/external slot → tray navigation using the returned structure and assignments. Occupied rows keep tapping the tray as the direct spool replacement path and provide a separate **Clear** action for unassignment. Clearing requires confirmation, calls the documented `DELETE /api/spools` route, checks the returned spool ID, and refreshes printer assignments after success. Tapping a tray loads active spools from the verified `GET /api/spools` route, with text search, material filtering, and paged results. Picker rows combine spool name, material, vendor, and ID, with remaining weight shown separately. Assigning a spool requires explicit confirmation and refreshes printer assignments after the API confirms success.

Open the serial monitor at 115200 baud to see the LVGL startup message and sampled touch coordinates. The starting raw calibration bounds are in `include/Config.h`; adjust them if the touch targets do not line up with the buttons.

This configuration is an initial match based on the board marking and the standard CYD reference. If the display remains blank or touch produces no readings, check the exact controller/board revision before changing pins or drivers.

The configured ILI9341 driver defaults to BGR channel order, and the LVGL flush routine byte-swaps RGB565 pixels before transfer. TFT inversion is not enabled by default. Open **Settings → Color Test** to compare labeled red, green, blue, orange, cyan, white, and black references. **Red/Blue: Normal/Swapped** changes RGB565 channel order in the display flush; **LCD Inversion: Off/On** toggles the controller inversion command. These checks are temporary and reset to defaults on reboot. The physical panel color order has not been independently verified on every ESP32-2432S028 revision.
