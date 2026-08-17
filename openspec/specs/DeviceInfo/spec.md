# DeviceInfo Plugin — Specification

## Overview

The **DeviceInfo** plugin exposes read-only device identity, hardware, firmware, network, and system information to JSON-RPC and COM-RPC clients running on WPEFramework (Thunder). It has no mutable state — all properties are derived at query time from system files, MFR library, RFC parameters, or DS (Device Settings) layer.

---

## Description

The DeviceInfo plugin is a WPEFramework (Thunder) plugin registered under the callsign `DeviceInfo` (versioned: `DeviceInfo.1`). It runs **in-process** (mode `"Off"`) inside the WPEFramework daemon — there is no separate process or IPC boundary.

The plugin shell (`DeviceInfo.cpp`) instantiates three implementation objects at startup via `IShell::Root<>()`, which resolves to local (same-process) COM object instantiation:

- **`DeviceInfoImplementation`** — implements `IDeviceInfo` and `IConfiguration`, provides all device identity, firmware, network and system properties.
- **`DeviceAudioCapabilities`** — implements `IDeviceAudioCapabilities`, exposes audio port capabilities via the DS layer.
- **`DeviceVideoCapabilities`** — implements `IDeviceVideoCapabilities`, exposes video display capabilities via the DS layer.

Auto-generated JSON-RPC bridge wrappers (`JDeviceInfo`, `JDeviceAudioCapabilities`, `JDeviceVideoCapabilities`) are registered at `Initialize()`, mapping all JSON-RPC calls directly to COM interface pointers without leaving the process.

---

## Requirements

- The plugin SHALL expose all device identity, firmware, network, and system properties as read-only JSON-RPC properties under the `DeviceInfo.1` callsign.
- All properties SHALL return `Core::ERROR_NONE` on success and `Core::ERROR_GENERAL` on failure, unless otherwise specified.
- The plugin SHALL run in-process (mode `Off`) inside the WPEFramework daemon.
- The `firmwareversion` property SHALL always populate `imagename` from `/version.txt`; `middleware` is optional and defaults to `"0.0"` if no version segment is found in `imagename`; all other optional fields default to `""` if not found.
- The `releaseversion` property SHALL always succeed, returning `"99.99.0.0"` as a default when the version cannot be parsed.

### Requirement: firmwareversion middleware field
The `firmwareversion` property SHALL include a `middleware` field in its response.
The `middleware` value SHALL be extracted from the `imagename` field (read from `/version.txt`) using a regex that matches a version segment of the form `N.Nxxx` (two numeric parts plus an alphanumeric suffix with no further dots) delimited by underscores or end of string.
If no such segment is found in `imagename`, the `middleware` field SHALL default to `"0.0"`.
The `middleware` field SHALL always be present in the response — it is never absent.

#### Scenario: imagename contains a direct version segment
- **WHEN** `imagename` is `ELTE11MWR_8.6p1s2_PROD`
- **THEN** `middleware` is `"8.6p1s2"`

#### Scenario: imagename contains a version embedded in a letter-prefixed segment
- **WHEN** `imagename` is `COESST11AEI_E032.031.00.8.6p99s2_DEV`
- **THEN** `middleware` is `"8.6p99s2"`

#### Scenario: imagename contains a multi-dot numeric version (not a middleware version)
- **WHEN** `imagename` is `SKTL11MEIIT_DEV_rel-15567_20260805034710_8.5.3.7B1`
- **THEN** `middleware` is `"0.0"`

#### Scenario: imagename has no version segment
- **WHEN** `imagename` is `ELTE11MWR_DEV_develop_20260806042826_DPRCTN`
- **THEN** `middleware` is `"0.0"`

#### Scenario: firmwareversion response always includes middleware field
- **WHEN** `firmwareversion` is called and `imagename` is successfully read
- **THEN** the response SHALL contain a `middleware` key regardless of whether a version was found
- The `brandname` property SHALL pre-set `brand` to `"Unknown"` before source lookup; this value is preserved in the output struct even on `ERROR_GENERAL`.
- The `addresses` property SHALL return the last IPv4 address per interface (by design).
- All MAC address and IP properties (`ethmac`, `estbmac`, `wifimac`, `estbip`) SHALL strip a trailing newline from the script output.
- The `devicetype` property SHALL convert `mediaclient` → `IpStb`, `hybrid` → `QamIpStb`, and any other value → `IpTv` when read from `/etc/device.properties`.
- The `modelname` property SHALL use `mfrSERIALIZED_TYPE_PROVISIONED_MODELNAME` as the primary source for devices with `DEVICE_NAME` equal to `PLATCO` or `LLAMA`.

---

## Architecture / Design

The DeviceInfo plugin runs **in-process** (mode `"Off"`) inside the WPEFramework daemon. There is no separate process or IPC boundary. The plugin shell (`DeviceInfo.cpp`) instantiates the three implementation objects via `IShell::Root<>()`, which resolves to a local (same-process) COM object instantiation. All JSON-RPC calls are dispatched directly to those COM interface pointers without leaving the process.

```
┌────────────────────────────────────────────────────────────────────────────┐
│                           Thunder Framework                                │
│                                                                            │
│  ┌──────────────────────────────────────────────────────────────────────┐  │
│  │                   DeviceInfo  (in-process, mode = Off)               │  │
│  │  IPlugin + JSONRPC                                                   │  │
│  │  Autogenerated stubs: JDeviceInfo, JDeviceAudioCapabilities,        │  │
│  │                        JDeviceVideoCapabilities                      │  │
│  │  IShell stored (AddRef in Initialize)                                │  │
│  │                                                                      │  │
│  │  ┌────────────────────────────────────────────────────────────────┐  │  │
│  │  │  DeviceInfoImplementation                                      │  │  │
│  │  │  (instantiated via service->Root<>(), same process)            │  │  │
│  │  │                                                                │  │  │
│  │  │  serialNumber  modelId  make  modelName  deviceType            │  │  │
│  │  │  socName  distributorId  brandName  chipSet  releaseVersion    │  │  │
│  │  │  firmwareVersion  systemInfo  addresses                        │  │  │
│  │  │  ethmac  estbmac  wifimac  estbip                             │  │  │
│  │  └────────────────────────────────────────────────────────────────┘  │  │
│  │                                                                      │  │
│  │  ┌──────────────────────────────────────────────────────────────┐    │  │
│  │  │  DeviceAudioCapabilities                                     │    │  │
│  │  │  audioCapabilities  ms12Capabilities  supportedMS12Profiles  │    │  │
│  │  └──────────────────────────────────────────────────────────────┘    │  │
│  │                                                                      │  │
│  │  ┌──────────────────────────────────────────────────────────────┐    │  │
│  │  │  DeviceVideoCapabilities                                     │    │  │
│  │  │  supportedDisplays  hostEDID  defaultResolution  supportedHdcp│   │  │
│  │  └──────────────────────────────────────────────────────────────┘    │  │
│  └──────────────────────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────────────────┘
         │                    │                    │                │
  ┌──────▼──────┐   ┌─────────▼──────────┐  ┌─────▼────────┐  ┌───▼──────────┐
  │ System Files│   │  MFR Library       │  │  RFC API     │  │  DS Layer    │
  │ /version.txt│   │  via IARM Bus      │  │  SerialNumber│  │  Audio/Video │
  │ device.props│   │  PDRI Provisioned  │  │  PartnerId   │  │  HDCP        │
  │ authSvc.conf│   └────────────────────┘  └──────────────┘  └──────────────┘
  │ partnerId   │
  │ manufacturer│   ┌────────────────────┐  ┌──────────────────────────────┐
  └─────────────┘   │ getDeviceDetails.sh│  │ Core SystemInfo              │
                    │ eth_mac estb_mac   │  │ Core AdapterIterator         │
                    │ wifi_mac estb_ip   │  │ CPU RAM Uptime Network IPv4  │
                    └────────────────────┘  └──────────────────────────────┘
```

### Key topology facts

| Fact | Detail |
|------|--------|
| Process mode | `Off` (in-process) — set in `CMakeLists.txt`, embedded in `DeviceInfo.config` |
| Instantiation | `IShell::Root<IDeviceInfo>()` resolves to a local COM object, no IPC |
| JSON-RPC bridge | Auto-generated `JDeviceInfo` / `JDeviceAudioCapabilities` / `JDeviceVideoCapabilities` wrappers registered at `Initialize()` |
| Implementation split | Three separate COM objects: `DeviceInfoImplementation`, `DeviceAudioCapabilities`, `DeviceVideoCapabilities` |
| Configuration | `DeviceInfoImplementation` also implements `IConfiguration`; `Configure(IShell*)` is called at startup to give it the service handle |
| Internal helpers | Three file-scoped helper functions in `DeviceInfoImplementation.cpp`: `GetFileRegex` (file + regex transport), `GetMFRData` (IARM helper), `GetRFCData` (RFC helper) |

### Design Decisions

| # | Question | Decision |
|---|----------|----------|
| 1 | `brandname` on failure | Returns `ERROR_GENERAL` **and** `brand` field is `"Unknown"` (the pre-set default is preserved in the output struct) |
| 3 | `releaseversion` version suffix | `[sp]` suffix (e.g., `22.03s`, `22.03p`) is guaranteed by image naming convention |
| 4 | PLATCO/LLAMA model name logic | Hardcoded device names — documented as-is, no config-driven approach |
| 5 | `addresses` IP field per interface | Returns the **last** IPv4 address per interface by design |

---

## External Interfaces

### JSON-RPC Properties

All properties are **read-only**. Return `Core::ERROR_NONE` on success, `Core::ERROR_GENERAL` on failure unless noted.

#### `serialnumber`
- **Response**: `{ "serialnumber": "<string>" }`
- **Data source chain** (first success wins):
  1. MFR library — `mfrSERIALIZED_TYPE_SERIALNUMBER` via IARM
  2. RFC parameter — `Device.DeviceInfo.SerialNumber`

#### `modelid`
- **Response**: `{ "sku": "<string>" }`
- **Data source chain** (first success wins):
  1. `/etc/device.properties` — `MODEL_NUM`
  2. MFR library — `mfrSERIALIZED_TYPE_MODELNAME`
  3. RFC parameter — `Device.DeviceInfo.ModelName`

#### `make`
- **Response**: `{ "make": "<string>" }`
- **Data source chain** (first success wins):
  1. MFR library — `mfrSERIALIZED_TYPE_MANUFACTURER`
  2. `/etc/device.properties` — `MFG_NAME`

#### `modelname`
- **Response**: `{ "model": "<string>" }`
- **Data source logic**: PLATCO/LLAMA → `mfrSERIALIZED_TYPE_PROVISIONED_MODELNAME` then `FRIENDLY_ID`; all others → `FRIENDLY_ID`

#### `devicetype`
- **Response**: `{ "devicetype": "IpTv" | "IpStb" | "QamIpStb" }`
- **Data source chain**: `/etc/authService.conf` → `/etc/device.properties` with conversion

#### `socname`
- **Response**: `{ "socname": "<string>" }`
- **Data source**: `/etc/device.properties` — `SOC`

#### `distributorid`
- **Response**: `{ "distributorid": "<string>" }`
- **Data source chain** (first success wins):
  1. `/opt/www/authService/partnerId3.dat`
  2. RFC parameter — `Device.DeviceInfo.X_RDKCENTRAL-COM_Syndication.PartnerId`

#### `brandname`
- **Response**: `{ "brand": "<string>" }`
- **Default**: `"Unknown"` preserved in struct even on failure
- **Data source chain** (first success wins):
  1. `/tmp/.manufacturer`
  2. MFR library — `mfrSERIALIZED_TYPE_MANUFACTURER`

#### `releaseversion`
- **Response**: `{ "releaseversion": "<major>.<minor>.0.0" }`
- **Always succeeds** — defaults to `"99.99.0.0"`
- **Data source**: `/version.txt` — `imagename:<value>`, regex `(\d+)\.(\d+)[sp]`

#### `chipset`
- **Response**: `{ "chipset": "<string>" }`
- **Data source**: `/etc/device.properties` — `CHIPSET_NAME`

#### `firmwareversion`
- **Response**: `{ "imagename", "middleware", "sdk", "mediarite", "yocto", "pdri" }`
- **Primary field**: `imagename` from `/version.txt`; failure returns `ERROR_GENERAL`
- **Optional fields**:

  | Field | Source |
  |-------|--------|
  | `middleware` | extracted from `imagename` via regex; defaults to `"0.0"` if no version segment found |
  | `sdk` | `/version.txt` — `SDK_VERSION` (defaults to `""`) |
  | `mediarite` | `/version.txt` — `MEDIARITE` (defaults to `""`) |
  | `yocto` | `/version.txt` — `YOCTO_VERSION` (defaults to `""`) |
  | `pdri` | MFR library — `mfrSERIALIZED_TYPE_PDRIVERSION` (defaults to `""`) |

#### `systeminfo`
- **Response**: version, uptime, totalram, freeram, totalswap, freeswap, devicename, cpuload, cpuloadavg, serialnumber, time
- **Always succeeds**

#### `addresses`
- **Response**: array of `{ "name", "mac", "ip" }`; IP = last IPv4 per interface
- **Always succeeds**

#### `ethmac` / `estbmac` / `wifimac` / `estbip`
- **Response**: `{ "eth_mac" }` / `{ "estb_mac" }` / `{ "wifi_mac" }` / `{ "estb_ip" }`
- **Data source**: `/lib/rdk/getDeviceDetails.sh` via `v_secure_popen`
- **Failure**: `ERROR_GENERAL` if `v_secure_popen` returns null

#### `supportedaudioports`
- **Response**: `{ "supportedAudioPorts": [...], "success": true }`
- **Data source**: DS layer — `device::Host::getAudioOutputPorts()`

### Usage Examples

Query `firmwareversion` via JSON-RPC:
```bash
curl -X POST http://localhost:9998/jsonrpc \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"DeviceInfo.1.firmwareversion"}'
# Example response:
# {"imagename":"ELTE11MWR_8.6p1s2_PROD","middleware":"8.6p1s2","sdk":"17.3","mediarite":"8.3.53","yocto":"dunfell","pdri":""}
```

Query `systeminfo` via JSON-RPC:
```bash
curl -X POST http://localhost:9998/jsonrpc \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":2,"method":"DeviceInfo.1.systeminfo"}'
```

### Error Behaviour Summary

| Property | Always succeeds? | Partial failure behaviour |
|----------|-----------------|--------------------------|
| `serialnumber` | No | — |
| `modelid` | No | — |
| `make` | No | — |
| `modelname` | No | — |
| `devicetype` | No | — |
| `socname` | No | — |
| `distributorid` | No | — |
| `brandname` | No | `brand` = `"Unknown"` |
| `releaseversion` | **Yes** | Defaults to `"99.99.0.0"` |
| `chipset` | No | — |
| `firmwareversion` | Partial | Optional fields default to `""` |
| `systeminfo` | **Yes** | — |
| `addresses` | **Yes** | Empty array |
| `ethmac` / `estbmac` / `wifimac` / `estbip` | No | — |
| `supportedaudioports` | No | — |

### System File Dependencies

| File | Properties |
|------|-----------|
| `/version.txt` | `firmwareversion`, `releaseversion` |
| `/etc/device.properties` | `modelid`, `make`, `modelname`, `devicetype`, `socname`, `chipset` |
| `/etc/authService.conf` | `devicetype` |
| `/opt/www/authService/partnerId3.dat` | `distributorid` |
| `/tmp/.manufacturer` | `brandname` |
| `/lib/rdk/getDeviceDetails.sh` | `ethmac`, `estbmac`, `wifimac`, `estbip` |

### External Library Dependencies

| Dependency | Used by |
|-----------|---------|
| IARM Bus / MFR Lib | `serialnumber`, `modelid`, `make`, `brandname`, `firmwareversion` (pdri), `modelname` (PLATCO/LLAMA) |
| RFC API (`rfcapi.h`) | `serialnumber`, `modelid`, `distributorid` |
| DS Layer | `supportedaudioports`, `DeviceAudioCapabilities`, `DeviceVideoCapabilities` |
| `v_secure_popen` | `ethmac`, `estbmac`, `wifimac`, `estbip` |
| `Core::SystemInfo` | `systeminfo` |
| `Core::AdapterIterator` | `addresses` |

---

## Performance

The DeviceInfo plugin is read-only with no caching layer. All properties are synchronous operations (file reads, IARM calls, or system API calls).

| Metric | Target | Notes |
|--------|--------|-------|
| Per-property latency | < 100 ms | File reads and regex matching are fast; IARM/MFR calls dominate |
| Memory footprint | Minimal | No heap-allocated caches; data is stack-allocated per call |
| CPU usage | Negligible | No background threads or polling |
| `v_secure_popen` calls | < 500 ms | Script-based properties (`ethmac`, `estbmac`, `wifimac`, `estbip`) are bounded by shell execution time |

No automated benchmark tests are currently defined. The goal is that no single property call blocks the Thunder main thread for more than 500 ms. IARM calls are the primary latency risk; their actual results are tracked during integration testing.

---

## Security

### Threat Analysis

| Threat | Risk | Mitigation |
|--------|------|------------|
| Shell injection via script arguments | Medium | `v_secure_popen` used instead of `popen`; arguments are hardcoded constants, no user input |
| Path traversal via file reads | Low | All file paths are hardcoded literals; no user-controlled input reaches file-open calls |
| Information disclosure | Low | Plugin exposes device identity data — access is restricted to authenticated Thunder clients via WPEFramework's built-in token/credential mechanism |
| IARM bus spoofing | Low | IARM is a local IPC bus; only privileged processes on the device can register as bus members |

### Security Requirements

- The plugin SHALL use `v_secure_popen` (not `popen`) for all shell invocations.
- All file paths used in `std::ifstream` opens SHALL be compile-time string literals — no runtime path construction.
- RFC API calls SHALL use fixed, compile-time parameter name strings — no dynamic parameter construction.
- The plugin SHALL NOT cache or persist any retrieved values across calls.

### Security Validation

- L1 tests verify that `v_secure_popen` is called (not `popen`) for MAC/IP properties via mock validation.
- Static analysis (scan) of `DeviceInfoImplementation.cpp` confirms no `popen` direct calls.
- Code review audit confirms all file paths are string literals with no runtime concatenation.

---

## Versioning & Compatibility

- **Callsign**: `DeviceInfo` / `DeviceInfo.1`
- **Plugin version**: 1.0.0 (Major.Minor.Patch)
- **Interface**: `IDeviceInfo` — all properties are read-only; no breaking changes expected from additive fields.
- **Configuration file**: `DeviceInfo.conf.in` / `DeviceInfo.config` — `PLUGIN_DEVICEINFO_MODE` controls process mode (default: `Off`).

---

## Conformance Testing & Validation

### Test Suites

- **L1 unit tests**: `Tests/L1Tests/tests/test_DeviceInfo.cpp`, `test_DeviceInfoJsonRpc.cpp`
  - Automated GTest suite covering each property: success paths, failure paths, fallback chains, edge cases, and boundary values.
  - Run via: `ctest --test-dir build/Tests/L1Tests -V`
- **L2 integration tests**: `Tests/L2Tests/tests/DeviceInfo_L2Test.cpp`
  - Automated GTest suite exercising JSON-RPC and COM-RPC end-to-end paths against mocked DS and IARM layers.
  - Run via: `ctest --test-dir build/Tests/L2Tests -V`

### CI Pipeline

- Both test suites run in the automated CI pipeline on every pull request.
- A failing test in either suite blocks merge.
- Test results are reported as pass/fail per test case in the pipeline output.

### Coverage Areas

| Area | Tests |
|------|-------|
| `firmwareversion` middleware extraction | L1: 3 tests (`WithMiddleware`, `DefaultWhenNoVersion`, `EmbeddedVersion`) |
| `firmwareversion` optional fields | L1: `FirmwareVersion_Success`, `FirmwareVersion_Success_MissingOptionalFields` |
| All JSON-RPC properties | L1: `test_DeviceInfoJsonRpc.cpp` — `handler.Exists()` and `handler.Invoke()` for each |
| COMRPC `FirmwareVersion` | L2: `DeviceInfo_COMRPC_FirmwareVersion*` |

All `firmwareversion` tests cover the optional fields (`sdk`, `mediarite`, `yocto`, `pdri`) and the `imagename` failure path.

---

## Covered Code

- plugin/DeviceInfo.cpp:
    - DeviceInfo::Initialize
    - DeviceInfo::Deinitialize
    - DeviceInfo::Information
    - DeviceInfo::Deactivated
- plugin/DeviceInfoImplementation.cpp:
    - DeviceInfoImplementation::Configure
    - DeviceInfoImplementation::SerialNumber
    - DeviceInfoImplementation::Sku
    - DeviceInfoImplementation::Make
    - DeviceInfoImplementation::Model
    - DeviceInfoImplementation::Brand
    - DeviceInfoImplementation::DeviceType
    - DeviceInfoImplementation::SocName
    - DeviceInfoImplementation::DistributorId
    - DeviceInfoImplementation::ReleaseVersion
    - DeviceInfoImplementation::ChipSet
    - DeviceInfoImplementation::FirmwareVersion
    - DeviceInfoImplementation::SystemInfo
    - DeviceInfoImplementation::Addresses
    - DeviceInfoImplementation::EthMac
    - DeviceInfoImplementation::EstbMac
    - DeviceInfoImplementation::WifiMac
    - DeviceInfoImplementation::EstbIp
    - DeviceInfoImplementation::SupportedAudioPorts
- plugin/DeviceAudioCapabilities.cpp:
    - DeviceAudioCapabilities::AudioCapabilities
    - DeviceAudioCapabilities::MS12Capabilities
    - DeviceAudioCapabilities::SupportedMS12AudioProfiles
- plugin/DeviceVideoCapabilities.cpp:
    - DeviceVideoCapabilities::SupportedVideoDisplays
    - DeviceVideoCapabilities::HostEDID
    - DeviceVideoCapabilities::DefaultResolution
    - DeviceVideoCapabilities::SupportedResolutions
    - DeviceVideoCapabilities::SupportedHdcp
- plugin/DeviceInfoImplementation.h:
    - DeviceInfoImplementation
- Tests/L1Tests/tests/test_DeviceInfo.cpp
- Tests/L1Tests/tests/test_DeviceInfoJsonRpc.cpp
- Tests/L2Tests/tests/DeviceInfo_L2Test.cpp

---

## Open Queries

_No open queries._

---

## References

- `entservices-apis/apis/DeviceInfo/IDeviceInfo.h` — COM interface definition
- `plugin/DeviceInfo.config` / `plugin/DeviceInfo.conf.in` — plugin configuration
- `plugin/CMakeLists.txt` — build configuration and mode defaults
- RDKEMW-276 — `DEVICE_TYPE` conversion rationale for `devicetype` property

---

## Change History

- 2026-08-06 - openspec-templater - Restructured to match spec template.
- 2026-08-06 - add-middleware-to-firmwareversion - Add middleware field to firmwareversion extracted from imagename


