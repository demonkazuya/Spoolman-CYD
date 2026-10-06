# SpoolmanSync API investigation

This document records the SpoolmanSync interface verified for the CYD client. The workspace contains the CYD development plan but not a local SpoolmanSync checkout, so source review used the upstream [`gibz104/SpoolmanSync`](https://github.com/gibz104/SpoolmanSync) `main` branch, and GET responses were checked against a running local instance on 2026-10-01.

These are the web application's routes used by its own UI, rather than a separately versioned/public API specification. Recheck the source before relying on them across SpoolmanSync upgrades.

## Confirmed routes

| Method | Path | Purpose | Evidence |
| --- | --- | --- | --- |
| `GET` | `/api/printers` | Discover printers, nested AMS units, trays, external spool slots, and current spool assignments in one response. | [`printers/route.ts`](https://github.com/gibz104/SpoolmanSync/blob/main/app/src/app/api/printers/route.ts) and live response |
| `GET` | `/api/spools` | Return active (non-archived) Spoolman spools, including nested filament/vendor details. | [`spools/route.ts`](https://github.com/gibz104/SpoolmanSync/blob/main/app/src/app/api/spools/route.ts) and live response |
| `POST` | `/api/spools` | Assign a spool to a tray. Request JSON: `{"spoolId": <number>, "trayId": "<string>"}`. | [`spools/route.ts`](https://github.com/gibz104/SpoolmanSync/blob/main/app/src/app/api/spools/route.ts) |
| `DELETE` | `/api/spools` | Unassign a spool. Request JSON: `{"spoolId": <number>}`. | [`spools/route.ts`](https://github.com/gibz104/SpoolmanSync/blob/main/app/src/app/api/spools/route.ts) |

No separate AMS, tray, current-assignment, spool-detail, or search route was found in the route tree. Use the combined printer response and spool list for those views. The handlers do not implement pagination or server-side spool search; spool filtering/search for the CYD should operate on the returned active-spool list unless a future source inspection establishes another route.

## `GET /api/printers`

The response is an object with `printers` and `automationsStale` properties. Each printer can contain `ams_units`, each with a `trays` array, plus an `external_spools` array. Tray objects include Home Assistant `entity_id`, stable `unique_id` when available, `tray_number`, and live filament fields such as `name`, `color`, `material`, and `remaining_weight`. Occupied entries can include a full `assigned_spool` object. Printers can expose additional entity IDs for print stage, weight, and progress.

The live instance returned two printers. One had one AMS with four tray entries and an external-spool slot. Its assigned spools were nested directly on tray/slot records. Empty trays were present in the same array. The observed response confirmed that tray counts and external slots must be taken from the response rather than hard-coded.

The route returns `400 {"error":"Home Assistant not configured"}` when no Home Assistant connection is configured, and `500 {"error":"Failed to fetch printers"}` on an unhandled failure.

## `GET /api/spools`

The response is `{ "spools": [...] }`. The route calls Spoolman for the spool list, filters out entries where `archived` is true, then returns the remaining spool records. The live response included spool `id`, `remaining_weight`, `initial_weight`, `used_weight`, `extra`, and nested `filament` data; the nested filament included `material`, `name`, `color_hex`, and nullable `vendor` data. The live request returned HTTP 200 and JSON without a cookie or authorization header.

The route returns `400 {"error":"Spoolman not configured"}` without a configured Spoolman connection and `500 {"error":"Failed to fetch spools"}` on an unhandled failure.

## Assignment and unassignment

The assignment handler validates `spoolId` as a finite JSON number and `trayId` as a non-empty JSON string. It passes those values to the Spoolman client and responds with `{ "spool": <updated spool> }` on success. It returns `400` with an `error` string for invalid fields or missing Spoolman configuration, and `500 {"error":"Failed to assign spool"}` on an unhandled failure.

Source evidence in [`spoolman.ts`](https://github.com/gibz104/SpoolmanSync/blob/main/app/src/lib/api/spoolman.ts) shows that the client stores the supplied tray key as JSON text in the spool's `extra.active_tray` field. The running instance's response confirms the key currently used: a tray's stable Home Assistant `unique_id`, for example `<PRINTER_SERIAL>_AMS_<AMS_SERIAL>_tray_1`. Send the returned `unique_id` when present, not the display name or numeric tray position. External spool slots also have `unique_id` values and appear assignable in the same way.

Assigning to an occupied tray first unassigns any other spool currently mapped to that same tray key. The CYD must therefore show a confirmation before calling this route. `DELETE /api/spools` accepts a numeric `spoolId`, clears that spool's `active_tray` value while preserving its other extra fields, and responds with `{ "spool": <updated spool> }`. It returns `400` for invalid IDs or missing Spoolman configuration and `500 {"error":"Failed to unassign spool"}` on an unhandled failure.

The two write routes are source-reviewed but were not invoked against the live instance, to avoid changing filament assignments during discovery.

## Connection, authentication, and transport

The local instance responded to both source-confirmed GET routes with HTTP 200 over plain HTTP and no authentication headers. This verifies the behavior of those GET requests for that instance; it does not establish that all deployments or write operations use the same access controls. The route source does not show an API token requirement in these handlers. There is no SpoolmanSync base path prefix in the tested routes.

## CYD client implications

- Load `/api/printers` to get the current printer/AMS/tray structure and assignment state.
- Load `/api/spools` for active spool inventory; show `id` so physically similar spools remain distinguishable.
- Use tray `unique_id` as `trayId` and spool numeric `id` as `spoolId`.
- Treat a non-2xx response as failure and display its JSON `error` when present. Only show success after a 2xx response containing the updated `spool`.
- Refresh `/api/printers` after a successful assignment or unassignment to obtain authoritative current state.
- Do not assume the API offers incremental search, pagination, or a separate spool detail endpoint.
