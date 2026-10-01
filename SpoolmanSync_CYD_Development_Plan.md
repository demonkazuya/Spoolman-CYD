# SpoolmanSync CYD Terminal — Development Plan

## 1. Project Goal

Build a standalone touchscreen terminal using an ESP32 Cheap Yellow Display (CYD), specifically the 2.8" 240×320 ESP32 display with touchscreen.

The terminal will provide a dedicated physical interface for assigning Spoolman spools to AMS trays without requiring a PC, phone, Home Assistant dashboard, or web browser.

### Target workflow

```text
CYD Terminal
    │
    │ Wi-Fi
    ▼
SpoolmanSync
    │
    ├── Spoolman
    │
    └── Home Assistant / Bambu integration
             │
             ▼
        Bambu Printer
             │
             ▼
            AMS
```

Primary user operation:

```text
Select Printer
    ↓
Select AMS
    ↓
Select Tray
    ↓
Select Spool
    ↓
Confirm
    ↓
Spool assigned
```

The CYD should be treated as a client/UI for SpoolmanSync, not as a second independent filament-management system.

---

# 2. Hardware Reference

Primary hardware reference:

- witnessmenow/ESP32-Cheap-Yellow-Display
- 2.8" 240×320 ESP32 touchscreen CYD
- ESP32
- Wi-Fi
- Bluetooth
- Resistive touchscreen
- TFT LCD
- Optional SD storage depending on board variant

Reference:

https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display

The exact CYD hardware variant must be identified before finalizing display/touch pin configuration.

---

# 3. Recommended Software Stack

Use:

- Arduino framework
- ESP32 Arduino Core
- LVGL
- LVGL_CYD or an equivalent CYD-specific display/touch initialization library
- WiFi
- HTTP/HTTPS client
- JSON parser such as ArduinoJson
- ESP32 Preferences/NVS for persistent configuration

Recommended architecture:

```text
Arduino
 ├── ESP32 Core
 ├── LVGL
 ├── CYD display/touch driver
 ├── WiFi
 ├── HTTP client
 ├── JSON
 └── Preferences/NVS
```

Avoid ESPHome for the initial implementation. This project needs a custom touchscreen UI and a dedicated API client, so a native Arduino/LVGL application is more appropriate.

---

# 4. Core Architecture

The firmware should be separated into layers.

```text
┌────────────────────────────────────┐
│              LVGL UI               │
│ Home / Printer / AMS / Spool /     │
│ Confirm / Settings                 │
└────────────────┬───────────────────┘
                 │
                 ▼
┌────────────────────────────────────┐
│       Application / State Layer     │
│ Current printer / AMS / tray /      │
│ selected spool / loading state      │
└────────────────┬───────────────────┘
                 │
                 ▼
┌────────────────────────────────────┐
│       SpoolmanSync API Client      │
│ GET/POST/etc. + JSON parsing       │
└────────────────┬───────────────────┘
                 │
                 ▼
┌────────────────────────────────────┐
│          Wi-Fi / Network           │
└────────────────┬───────────────────┘
                 │
                 ▼
             SpoolmanSync
```

The UI must not directly construct HTTP requests.

Instead:

```cpp
spoolmanSync.getPrinters();
spoolmanSync.getAMS(printerId);
spoolmanSync.getTrays(printerId, amsId);
spoolmanSync.getSpools();
spoolmanSync.searchSpools(query);
spoolmanSync.assignSpool(spoolId, printerId, trayId);
spoolmanSync.unassignSpool(printerId, trayId);
```

The exact methods/endpoints must be based on the actual current SpoolmanSync implementation, not assumptions.

---

# 5. Most Important First Task — API Discovery

Before implementing the application UI, inspect the current SpoolmanSync source/API.

The current project uses SpoolmanSync as the backend responsible for assigning spools to AMS/CFS trays.

The development team must determine the real current API routes and payloads.

Investigate:

```text
GET  printers
GET  AMS/CFS units
GET  trays
GET  current assignments
GET  spool inventory
GET  individual spool details
SEARCH spools
ASSIGN spool to tray
UNASSIGN spool
```

Also determine:

- API base URL
- API port
- authentication requirements
- HTTP vs HTTPS
- request methods
- request payloads
- response JSON
- error responses
- printer/AMS/tray identifiers
- Spoolman spool ID handling
- pagination
- filtering/search capabilities
- whether the existing web UI has internal API endpoints that can be reused

Do NOT invent endpoint names.

Use the actual SpoolmanSync source code and documentation.

---

# 6. V1 Functional Requirements

The first usable firmware should support:

- Wi-Fi connection
- LVGL touchscreen UI
- SpoolmanSync connection
- Printer discovery
- AMS/CFS discovery
- Tray discovery
- Current tray assignments
- Spool inventory
- Spool search
- Material filtering
- Spool ID display
- Remaining weight display
- Assign spool
- Unassign spool
- Assignment confirmation
- Success message
- Error message
- Retry
- Refresh
- Basic settings

Do not implement QR/NFC/camera functionality in V1.

---

# 7. User Interface

## 7.1 Startup Screen

```text
┌──────────────────────────────┐
│       SPOOLMAN SYNC          │
│                              │
│            CYD               │
│                              │
│       Touch to Start         │
│                              │
│ WiFi: ● Connected             │
└──────────────────────────────┘
```

Startup should:

1. Initialize hardware.
2. Initialize LVGL.
3. Connect to Wi-Fi.
4. Test SpoolmanSync availability.
5. Load configuration.
6. Enter Home screen.

If the network is unavailable, provide a useful error state and access to Settings.

---

# 8. Home Screen

Example:

```text
┌──────────────────────────────┐
│ SPOOLMAN SYNC       WiFi ●   │
├──────────────────────────────┤
│                              │
│       SELECT PRINTER         │
│                              │
│ ┌────────────┐ ┌────────────┐│
│ │    P1S     │ │    P1S     ││
│ │ Printer 1  │ │ Printer 2  ││
│ └────────────┘ └────────────┘│
│                              │
│                              │
│ [ SETTINGS ]                 │
└──────────────────────────────┘
```

Do not hard-code printer names.

Load printers dynamically from SpoolmanSync.

---

# 9. AMS Screen

Example:

```text
┌──────────────────────────────┐
│ ← P1S #1        AMS 1        │
├──────────────────────────────┤
│                              │
│ ┌──────┐  ┌──────┐           │
│ │  1   │  │  2   │           │
│ │ PLA  │  │ PETG │           │
│ │WHITE │  │BLACK │           │
│ └──────┘  └──────┘           │
│                              │
│ ┌──────┐  ┌──────┐           │
│ │  3   │  │  4   │           │
│ │EMPTY │  │ PLA  │           │
│ │      │  │ RED  │           │
│ └──────┘  └──────┘           │
│                              │
│ [HOME]              [REFRESH]│
└──────────────────────────────┘
```

The UI should support any number of trays returned by the backend rather than assuming exactly four.

---

# 10. Spool Selection Screen

Example:

```text
┌──────────────────────────────┐
│ ← AMS 1       TRAY 1         │
├──────────────────────────────┤
│ Current: EMPTY               │
│                              │
│ [ Search...              ]   │
│                              │
│ PLA                          │
│ ┌──────────────────────────┐ │
│ │ Sunlu PLA White           │ │
│ │ 742g   ID #127            │ │
│ └──────────────────────────┘ │
│                              │
│ ┌──────────────────────────┐ │
│ │ Sunlu PLA White           │ │
│ │ 356g   ID #183            │ │
│ └──────────────────────────┘ │
│                              │
│ ┌──────────────────────────┐ │
│ │ Inslogic Matte White      │ │
│ │ 821g   ID #201            │ │
│ └──────────────────────────┘ │
└──────────────────────────────┘
```

Important:

Spoolman ID must be visible.

Identical filament names must remain distinguishable.

Example:

```text
Sunlu PLA White
ID #127
742g
```

versus:

```text
Sunlu PLA White
ID #183
356g
```

---

# 11. Spool Filtering

V1 should support:

### Material

```text
PLA
PETG
ABS
ASA
TPU
PA
PC
...
```

### Search

Example:

```text
[ WHITE________ ]
```

### Vendor

Recommended for V1.1:

```text
Sunlu
Kingroon
Inslogic
Dowell
Bambu
...
```

### Future filters

- Color
- Weight
- Available/assigned
- Recently used
- Favorites

---

# 12. Confirmation Screen

Never assign immediately after selecting a spool.

Show:

```text
┌──────────────────────────────┐
│       CONFIRM ASSIGNMENT     │
├──────────────────────────────┤
│ Printer: P1S #1              │
│ AMS:     AMS 1               │
│ Tray:    1                   │
│                              │
│ Spool:                       │
│ Sunlu PLA White              │
│ ID #127                      │
│                              │
│ Remaining: 742g              │
│                              │
│ [ CANCEL ]       [ ASSIGN ]  │
└──────────────────────────────┘
```

After assignment:

```text
ASSIGNING...
```

Then:

```text
✓ ASSIGNED

Tray 1
Sunlu PLA White
742g
```

---

# 13. Unassign Workflow

Selecting an occupied tray should provide:

```text
CURRENT SPOOL

Sunlu PLA White
ID #127
742g

[ CHANGE SPOOL ]
[ UNASSIGN ]
[ CANCEL ]
```

Unassign should also have confirmation.

---

# 14. Network Configuration

Store these values using ESP32 Preferences/NVS:

```text
WiFi SSID
WiFi Password
SpoolmanSync Host
SpoolmanSync Port
Optional API Token
```

Do not hard-code server IP addresses in production firmware.

Support hostnames.

Example:

```text
spoolmansync.local
```

or a local DNS hostname.

A future setup screen should provide:

```text
┌──────────────────────────────┐
│       NETWORK SETTINGS       │
├──────────────────────────────┤
│ WiFi                         │
│ HomeNetwork                  │
│                              │
│ SpoolmanSync                 │
│ 192.168.1.xxx                │
│                              │
│ Port                         │
│ 3000                         │
│                              │
│ [ TEST CONNECTION ]          │
│                              │
│ [ SAVE ]                     │
└──────────────────────────────┘
```

---

# 15. Error Handling

The device must gracefully handle:

### Wi-Fi failure

```text
WiFi disconnected

[ RETRY ]
[ SETTINGS ]
```

### SpoolmanSync unavailable

```text
Cannot connect to SpoolmanSync

Server:
192.168.1.xxx:3000

[ RETRY ]
[ SETTINGS ]
```

### API error

Show a short human-readable error and allow retry.

### Assignment failure

Never display success unless the backend confirms the operation.

Example:

```text
Assignment failed.

Server returned:
<short error>

[ RETRY ]
[ BACK ]
```

---

# 16. Refresh Strategy

Avoid continuously downloading the complete spool inventory.

Recommended approach:

- Load printer/AMS information when entering relevant screens.
- Load current tray assignments when opening AMS.
- Search/filter spools on demand.
- Cache recently loaded data.
- Provide manual Refresh.
- Add controlled automatic refresh later.

The device should not hammer the SpoolmanSync server.

---

# 17. Persistent State

Store:

```text
WiFi configuration
SpoolmanSync host
SpoolmanSync port
Optional API credentials
Screen brightness
Screen timeout
Last selected printer
Last selected AMS
```

Do NOT permanently cache assignments as authoritative data.

The backend remains authoritative.

---

# 18. Suggested Source Tree

```text
SpoolmanCYD/
│
├── src/
│   ├── main.cpp
│   │
│   ├── config/
│   │   ├── Config.h
│   │   └── Secrets.h
│   │
│   ├── network/
│   │   ├── WiFiManager.cpp
│   │   └── WiFiManager.h
│   │
│   ├── api/
│   │   ├── SpoolmanSyncClient.cpp
│   │   ├── SpoolmanSyncClient.h
│   │   ├── ApiModels.h
│   │   └── ApiParser.cpp
│   │
│   ├── ui/
│   │   ├── UI.cpp
│   │   ├── UI.h
│   │   ├── HomeScreen.cpp
│   │   ├── PrinterScreen.cpp
│   │   ├── AMScreen.cpp
│   │   ├── SpoolScreen.cpp
│   │   ├── ConfirmScreen.cpp
│   │   └── SettingsScreen.cpp
│   │
│   └── storage/
│       ├── Preferences.cpp
│       └── Preferences.h
│
├── include/
├── platformio.ini
├── README.md
└── docs/
    ├── API.md
    ├── UI.md
    └── HARDWARE.md
```

PlatformIO is recommended for the development project because dependency management and build configuration will become useful as the firmware grows. Arduino IDE can still be used for initial hardware bring-up.

---

# 19. Development Milestones

## M0 — Identify Hardware

- Confirm exact CYD variant.
- Confirm display controller.
- Confirm touch controller.
- Confirm ESP32 variant.
- Confirm orientation.
- Confirm backlight control.
- Confirm SD slot if present.

Deliverable:

```text
Known-good CYD hardware configuration
```

---

## M1 — LCD + Touch

Create a minimal LVGL application.

Requirements:

- LCD works.
- Touch works.
- Correct orientation.
- Correct resolution.
- Touch coordinates align with UI.
- Backlight works.

Deliverable:

```text
Touchscreen test application
```

---

## M2 — LVGL Framework

Implement:

- Main screen.
- Navigation.
- Buttons.
- Labels.
- Lists.
- Loading indicator.
- Dialog.
- Status bar.

Deliverable:

```text
Reusable UI framework
```

---

## M3 — Wi-Fi

Implement:

- Wi-Fi connection.
- Reconnection.
- Connection status.
- Persistent credentials.
- Settings screen.

Deliverable:

```text
CYD reliably connects to LAN
```

---

## M4 — SpoolmanSync API Discovery

This is a critical milestone.

Inspect the current SpoolmanSync source/documentation.

Document:

```text
Endpoint
Method
Request
Response
Errors
Authentication
```

Deliverable:

```text
docs/API.md
```

Do not continue with guessed API endpoints.

---

## M5 — ESP32 API Client

Implement:

```cpp
getPrinters()
getAMS()
getTrays()
getAssignments()
getSpools()
searchSpools()
assignSpool()
unassignSpool()
```

Use actual endpoints discovered in M4.

Deliverable:

```text
Testable SpoolmanSyncClient
```

---

## M6 — Printer / AMS / Tray UI

Implement dynamic discovery.

Flow:

```text
Home
 ↓
Printer
 ↓
AMS
 ↓
Tray
```

Deliverable:

```text
User can navigate the real AMS structure
```

---

## M7 — Spool Inventory

Implement:

- Search
- Material filtering
- Vendor display
- Spool ID
- Remaining weight
- Selection

Deliverable:

```text
User can select a real Spoolman spool
```

---

## M8 — Assignment

Implement:

```text
Tray
 ↓
Spool
 ↓
Confirmation
 ↓
SpoolmanSync
 ↓
Success
```

Deliverable:

```text
CYD can assign a real spool to a real AMS tray.
```

---

## M9 — Unassignment

Status: Implemented and physically verified. The user confirmed clearing/unassigning works on the CYD.

Implement:

```text
Occupied tray
 ↓
Unassign
 ↓
Confirmation
 ↓
SpoolmanSync
```

Deliverable:

```text
CYD can remove an assignment.
```

---

## M10 — Reliability

Status: Core recovery paths have been exercised on the device: Wi-Fi reconnect after restart, incorrect Wi-Fi password timeout/error, SpoolmanSync unavailable without reboot, manual refresh after the server returns, and assignment success. The remaining API validation cases below have not all been exercised on hardware.

Test:

- Wi-Fi disconnect
- Server restart
- API timeout
- Invalid spool
- Invalid tray
- Assignment conflict
- Repeated assignment
- Server unavailable
- Device reboot
- Touch errors

Deliverable:

```text
Reliable LAN terminal
```

---

## M11 — UI Polish

Status: Main navigation, printer refresh, theme toggle, and compact spool display are implemented. The user deferred remaining landscape text/layout polish. Physical display color order still needs visual confirmation on the user's specific panel revision.

Improve:

- Typography
- Icons
- Colors
- Spool colors
- Loading indicators
- Animations
- Navigation
- Touch targets
- Error dialogs
- Status indicators

Important:

Because the screen is only 240×320, prioritize large touch targets and readable text over displaying excessive information.

---

## M12 — OTA

Status: Implemented in firmware `0.1.0` and published as a public GitHub Release. The update page checks the latest release, validates a repository-scoped HTTPS manifest, verifies size and SHA-256, then installs to the inactive OTA slot. Physical OTA installation has not yet been tested; `0.1.0` is the initial release, so it correctly reports no update until a later version is published.

Implemented:

- Firmware version.
- OTA update mechanism.
- Version display.

The public GitHub release workflow publishes a full-flash image, app-only OTA image, and `ota.json` manifest. The user's installed image must be OTA-capable before it can use the updater. Keep the optional custom update server out of V1 unless requested; GitHub Releases is the configured update source.

---

# 20. V1.1 Features

After the basic terminal works:

- Vendor filter
- Color filter
- Sort by remaining weight
- Recently used spools
- Favorites
- Better caching
- Screen timeout
- Brightness control
- Automatic refresh
- Better offline behavior
- Multiple CYD terminals

---

# 21. V2 Features

Potential future features:

## QR scanning

The CYD itself does not normally have a camera.

Possible future hardware:

```text
CYD
 +
Camera module
```

Workflow:

```text
SCAN SPOOL
    ↓
Spoolman ID
    ↓
Select Tray
    ↓
Assign
```

SpoolmanSync already supports QR/NFC-oriented spool workflows, so this would be a natural future enhancement.

## NFC

Potential workflow:

```text
Tap NFC spool
    ↓
Read Spool ID
    ↓
Select Tray
    ↓
Assign
```

This could eventually be more convenient than QR for physical filament management.

---

# 22. Future Quick Assign Mode

Once the basic workflow is stable, implement a faster interface.

Example:

```text
AMS 1

[ 1 ] [ 2 ]
[ 3 ] [ 4 ]

Select tray
    ↓
Select spool
    ↓
Assign
```

The goal is to minimize the number of screen interactions.

Potentially add:

```text
Recently used
Favorites
Last used spools
```

---

# 23. Design Principles

The following principles should guide development.

### 1. SpoolmanSync is authoritative

The CYD is a client.

Do not create a second database.

### 2. Do not hard-code printers

Discover them from the backend.

### 3. Do not assume four trays

Render the tray structure returned by the backend.

### 4. Always show Spoolman ID

Identical filament products can have different physical spools.

### 5. Never report success without backend confirmation

The UI must reflect actual API results.

### 6. Keep the UI simple

The terminal is designed for physical use beside a printer.

### 7. Optimize for touch

Buttons should be large enough for reliable finger interaction.

### 8. Avoid unnecessary network traffic

Use caching and explicit refresh where appropriate.

### 9. Separate UI and API logic

This makes future backend/API changes much easier.

### 10. Build incrementally

Do not implement QR, NFC, OTA, animations, etc. before basic assignment works.

---

# 24. Initial Project Definition for Codex

Codex should treat the following as the project objective:

> Build a native Arduino/LVGL firmware for a 2.8" 240×320 ESP32 Cheap Yellow Display that acts as a standalone SpoolmanSync touchscreen terminal.
>
> The terminal must allow the user to discover printers, AMS/CFS units, trays, and Spoolman inventory; select a specific physical spool by Spoolman ID; and assign or unassign that spool from an AMS tray through SpoolmanSync.
>
> The CYD must communicate with SpoolmanSync over the local Wi-Fi network. It must not independently implement the Bambu/AMS synchronization logic.
>
> The implementation must use the actual current SpoolmanSync API discovered from the project's source code/documentation. Do not invent API endpoints.
>
> Build the project incrementally, beginning with CYD hardware bring-up, then LVGL, Wi-Fi, API discovery, API client, printer/AMS/tray UI, spool selection, assignment, and reliability testing.

---

# 25. Current Status and Next Actions

The project has completed the main V1 implementation and is published at `https://github.com/demonkazuya/Spoolman-CYD`. The initial OTA-capable release is `v0.1.0`. The remaining work is verification and polish rather than initial implementation:

1. Flash `spoolman-cyd-v0.1.0-full.bin` at `0x0` and confirm first-run Wi-Fi and SpoolmanSync setup.
2. Check physical dark/light theme colors. The driver defaults to BGR; if orange appears blue/cyan while black and white look normal, verify the panel's red/blue order before changing the driver configuration. See `docs/HARDWARE.md`.
3. For OTA hardware verification, install `v0.1.0`, then publish a later stable version and confirm that Settings finds it, downloads it, verifies it, restarts, and reports the newer version.
4. Continue the deferred landscape typography/layout polish.
5. Exercise remaining reliability cases where safe, including malformed API records and invalid assignment responses.

---

# 26. Definition of Done — V1

V1 is complete when the following works on the physical CYD:

```text
Power on
   ↓
Wi-Fi connects
   ↓
SpoolmanSync detected
   ↓
Printer list appears
   ↓
Select printer
   ↓
AMS list appears
   ↓
Select AMS
   ↓
Tray assignments appear
   ↓
Select tray
   ↓
Search/select spool
   ↓
Confirm
   ↓
SpoolmanSync assigns spool
   ↓
CYD confirms success
   ↓
AMS view updates
```

And:

```text
Occupied tray
   ↓
Select tray
   ↓
Unassign
   ↓
Confirm
   ↓
SpoolmanSync removes assignment
   ↓
CYD updates tray to EMPTY
```

At this point the device has achieved its primary goal: a standalone physical terminal for managing filament assignments without using a PC or phone.
