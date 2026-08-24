## Context

The `DeviceInfo` plugin exposes device metadata over JSON-RPC and COM-RPC. Serial numbers obtained via `mfrSERIALIZED_TYPE_SERIALNUMBER` may be purely numeric on some platforms, making them unsuitable as unique device identifiers without further disambiguation. A manufacturing serial number (`mfrSERIALIZED_TYPE_MANUFACTURING_SERIALNUMBER`) provides an alphanumeric value on those platforms.

Callers currently have no canonical property to obtain a stable, partner-agnostic device identity. The `hardwareId` (first 6 characters of `deviceId`) provides a compact hardware fingerprint used by several partner integrations.

Both properties are read-only and static for the lifetime of the plugin instance, making caching appropriate and safe.

## Goals / Non-Goals

**Goals:**
- Expose `deviceId` as a new JSON-RPC / COM-RPC property on `DeviceInfo.1`.
- Expose `hardwareId` as a new JSON-RPC / COM-RPC property on `DeviceInfo.1`.
- Cache `deviceId` on first resolution so `hardwareId` reuses it at zero extra cost.
- Implement the alphanumeric detection in a UB-free manner (cast to `unsigned char` before `std::isdigit`).

**Non-Goals:**
- Modifying any existing properties (`serialnumber`, `modelid`, etc.).
- Persisting the cache across plugin restarts (in-memory only).
- Supporting a writeable or configurable device ID.

## Decisions

### D1 — Resolution logic for `deviceId`

Retrieve `mfrSERIALIZED_TYPE_SERIALNUMBER`. If the value contains at least one non-digit character it is alphanumeric and is used directly. Otherwise retrieve `mfrSERIALIZED_TYPE_MANUFACTURING_SERIALNUMBER` and use that value. If the manufacturing serial number call also fails, `deviceId` is set to `""`.

**Alternative considered**: always prefer manufacturing serial number. Rejected because alphanumeric serial numbers are already unique and the manufacturing serial is not available on all platforms.

### D2 — Caching via `mutable` members

`DeviceID()` is a `const` method (COM-RPC interface constraint). A `mutable string _cachedDeviceID` and `mutable bool _deviceIDCached` are added to `DeviceInfoImplementation`. On the first call the value is resolved and stored; subsequent calls return immediately.

**Alternative considered**: a `std::once_flag` + `std::call_once`. Rejected as heavier than needed given the plugin is single-threaded at init time and the flag+string pair is simpler and equally correct.

### D3 — `hardwareId` delegates to `DeviceID()`

`HardwareID()` calls `DeviceID()` internally and takes `substr(0, 6)`. This guarantees `hardwareId` is always consistent with `deviceId` and benefits from the same cache without any additional state.

### D4 — Alphanumeric detection

`std::all_of` with a lambda `[](unsigned char c){ return std::isdigit(c); }` is used. Casting to `unsigned char` prevents undefined behaviour when `char` is signed and a byte value is negative.

## Risks / Trade-offs

- **[Risk] MFR library unavailable at query time** → `DeviceID()` returns `Core::ERROR_NONE` with an empty string (soft failure). Callers should handle empty `deviceId` gracefully.
- **[Risk] Cache is never invalidated** → Acceptable: hardware identity is immutable for the lifetime of a running device. A plugin restart clears the cache.
- **[Risk] `substr(0,6)` on a `deviceId` shorter than 6 chars** → `std::string::substr` with a length larger than `size()` returns the whole string; no crash, but `hardwareId` may be shorter than 6 characters. This is the defined behaviour per the spec.
- **[Trade-off] Empty `deviceId` on full MFR failure** → preferred over propagating an error code, so `hardwareId` callers always receive a valid (possibly empty) string rather than an error path.
