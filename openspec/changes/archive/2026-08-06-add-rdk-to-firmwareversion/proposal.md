## Why

The `firmwareversion` JSON-RPC property currently does not expose the rdk version of the device software stack. Operators and diagnostics tools need this field to identify which rdk build is running on a device, which is critical for triaging firmware vs. rdk issues.

## What Changes

- Add a new `rdk` field to the `firmwareversion` API response.
- The `rdk` value is extracted from the `imagename` field already present in `/version.txt`, using a regex to parse the version segment (e.g. `ELTE11MWR_8.6p1s2_PROD` → `8.6p1s2`).
- If no version segment matching the pattern is found in `imagename`, the field defaults to `"0.0"`.
- The `rdk` field is always present in the response (never absent), even when its value is the default.

## Capabilities

### New Capabilities

_(none — this extends an existing capability)_

### Modified Capabilities

- `DeviceInfo`: The `firmwareversion` property response gains a new `rdk` field. This is an additive, non-breaking change to the existing response schema.

## Impact

- **Code**: `plugin/DeviceInfoImplementation.cpp` — `FirmwareVersion()` method updated to extract and populate `rdk` from `imagename`.
- **Interface**: `entservices-apis/apis/DeviceInfo/IDeviceInfo.h` — `FirmwareversionInfo` struct gains `rdk` member.
- **Tests**: L1 tests (`test_DeviceInfo.cpp`, `test_DeviceInfoJsonRpc.cpp`) and L2 tests (`DeviceInfo_L2Test.cpp`) updated to cover the new field.
- **Spec**: `openspec/specs/DeviceInfo/spec.md` updated with rdk field documentation.
- **No breaking changes** — existing consumers that ignore unknown fields are unaffected; the field is additive.
