## Why

The DeviceInfo plugin currently lacks a region/partner-agnostic way to identify a device uniquely. Serial numbers from MFR can be purely numeric (non-unique across partners) or alphanumeric, requiring callers to implement their own disambiguation logic. Centralising this in the plugin removes fragmentation and ensures a consistent, stable device identity and hardware fingerprint across all RDK deployments.

## What Changes

- Add a new `deviceId` property to `DeviceInfo` that returns a stable, alphanumeric device identifier resolved from MFR serial data.
- Add a new `hardwareId` property to `DeviceInfo` that returns the first 6 characters of `deviceId` as a hardware fingerprint.
- `deviceId` is cached after the first resolution to avoid repeated MFR library calls.
- No existing properties are removed or modified.

## Capabilities

### New Capabilities

- `device-id`: Exposes `DeviceInfo.deviceId` property — resolves `mfrSERIALIZED_TYPE_SERIALNUMBER`; if alphanumeric returns it directly, otherwise falls back to `mfrSERIALIZED_TYPE_MANUFACTURING_SERIALNUMBER`. Result is cached.
- `hardware-id`: Exposes `DeviceInfo.hardwareId` property — returns the first 6 characters of the cached `deviceId`.

### Modified Capabilities

<!-- No existing spec-level requirements are changing. -->

## Impact

- **API**: Two new read-only properties exposed over JSON-RPC and COM-RPC on the `DeviceInfo.1` plugin.
- **Interface**: `IDeviceInfo` gains `DeviceID(DeviceIdInfo&)` and `HardwareID(HardwareIdInfo&)` virtual methods.
- **Implementation**: `DeviceInfoImplementation` adds mutable cache members `_cachedDeviceID` / `_deviceIDCached` to serve both properties without repeated IARM calls.
- **Tests**: New L1 (unit) and L2 (integration) test cases covering both properties and all branching paths.
- **Dependencies**: Requires `IARM_BUS_MFRLIB_API_GetSerializedData` with `mfrSERIALIZED_TYPE_SERIALNUMBER` and `mfrSERIALIZED_TYPE_MANUFACTURING_SERIALNUMBER`.
