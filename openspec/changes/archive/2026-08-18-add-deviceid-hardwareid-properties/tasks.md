## 1. Interface — IDeviceInfo

- [x] 1.1 Add `DeviceIdInfo` struct with `string deviceId` field to `IDeviceInfo.h` (if not already present)
- [x] 1.2 Add `HardwareIdInfo` struct with `string hardwareId` field to `IDeviceInfo.h`
- [x] 1.3 Add pure virtual `DeviceId(DeviceIdInfo& /* @out */) const = 0` method to `IDeviceInfo`
- [x] 1.4 Add pure virtual `HardwareId(HardwareIdInfo& /* @out */) const = 0` method to `IDeviceInfo`

## 2. Implementation — DeviceInfoImplementation

- [x] 2.1 Add `mutable string _cachedDeviceID` and `mutable bool _deviceIDCached { false }` to `DeviceInfoImplementation` private section in `DeviceInfoImplementation.h`
- [x] 2.2 Declare `DeviceId(DeviceIdInfo&) const override` in `DeviceInfoImplementation.h`
- [x] 2.3 Declare `HardwareId(HardwareIdInfo&) const override` in `DeviceInfoImplementation.h`
- [x] 2.4 Implement `DeviceInfoImplementation::DeviceId()` in `DeviceInfoImplementation.cpp`:
  - Return cached value if `_deviceIDCached` is true
  - Call `SerialNumber()` to get `mfrSERIALIZED_TYPE_SERIALNUMBER`
  - If alphanumeric: use as `deviceID` directly
  - If numeric-only: call `GetMFRData(mfrSERIALIZED_TYPE_MANUFACTURING_SERIALNUMBER, ...)`; set empty string on failure
  - Store result in `_cachedDeviceID`, set `_deviceIDCached = true`
- [x] 2.5 Implement `DeviceInfoImplementation::HardwareId()` in `DeviceInfoImplementation.cpp`:
  - Call `DeviceId()` to get (or reuse cached) device ID
  - Set `hardwareID = deviceId.substr(0, 6)`
  - Return same `hresult` as `DeviceId()`

## 3. JSON-RPC Registration — DeviceInfo plugin

- [x] 3.1 Register `deviceid` property handler in `DeviceInfo.cpp` that calls `DeviceId()` and serialises `DeviceIdInfo`
- [x] 3.2 Register `hardwareid` property handler in `DeviceInfo.cpp` that calls `HardwareId()` and serialises `HardwareIdInfo`

## 4. L1 Tests — DeviceInfoTest

- [x] 4.1 Add `DeviceID_AlphanumericSerial_UsesSerialNumber`: serial `EB21163216C000024` → `deviceId` = `EB21163216C000024`
- [x] 4.2 Add `DeviceID_NumericSerial_UsesMfgSerialNumber`: serial `84725041828384`, mfg serial `IP09SK925314001D0` → `deviceId` = `IP09SK925314001D0`
- [x] 4.3 Add `DeviceID_NumericSerial_MfgSerialFails_ReturnsEmpty`: numeric serial, mfg call fails → `deviceId` = `""`, return `Core::ERROR_NONE`
- [x] 4.4 Add `DeviceID_SerialNumberFails_ReturnsError`: both MFR and RFC fail → `Core::ERROR_GENERAL`
- [x] 4.5 Add `HardwareID_Returns_First6_Alphanumeric`: `deviceId` = `EB21163216C000024` → `hardwareId` = `EB2116`
- [x] 4.6 Add `HardwareID_Returns_First6_MfgSerial`: `deviceId` = `IP09SK925314001D0` → `hardwareId` = `IP09SK`
- [x] 4.7 Add `HardwareID_Empty_WhenDeviceIdEmpty`: `deviceId` = `""` → `hardwareId` = `""`, return `Core::ERROR_NONE`
- [x] 4.8 Add `HardwareID_Short_DeviceId`: `deviceId` shorter than 6 chars → `hardwareId` equals full `deviceId`

## 5. L2 Tests — DeviceInfo_L2test

- [x] 5.1 Add JSON-RPC `DeviceInfo_JsonRpc_DeviceID_AlphanumericSerial_UsesSerialNumber`
- [x] 5.2 Add JSON-RPC `DeviceInfo_JsonRpc_DeviceID_NumericSerial_UsesMfgSerialNumber`
- [x] 5.3 Add JSON-RPC `DeviceInfo_JsonRpc_DeviceID_NumericSerial_MfgFails_ReturnsEmpty`
- [x] 5.4 Add JSON-RPC `DeviceInfo_JsonRpc_HardwareID_ReturnsFirst6OfDeviceId`
- [x] 5.5 Add COM-RPC `DeviceInfo_COMRPC_DeviceID_AlphanumericSerial`
- [x] 5.6 Add COM-RPC `DeviceInfo_COMRPC_DeviceID_NumericSerial_UsesMfgSerialNumber`
- [x] 5.7 Add COM-RPC `DeviceInfo_COMRPC_HardwareID_ReturnsFirst6OfDeviceId`
- [x] 5.8 Add COM-RPC `DeviceInfo_COMRPC_HardwareID_Empty_WhenDeviceIdEmpty`
