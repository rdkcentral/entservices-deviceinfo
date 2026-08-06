## Why

Applications need to collect operating system name and version information for telemetry purposes and to provide customer support tailored to specific OS versions. Currently, the DeviceInfo plugin does not support retrieving or persisting this OS metadata. This change enables operators to configure and persist OS details that survive reboots but are cleared on factory reset.

## What Changes

- Add `osName` property to DeviceInfo plugin (get/set operations, string data type)
- Add `osVersion` property to DeviceInfo plugin (get/set operations, string data type)
- Implement persistent storage for both properties using `/opt/persistent/osdetails.info` file with key-value format
- Return empty string for get operations if values have not been set yet
- Ensure persisted values survive device reboots
- Ensure persisted values are wiped during factory reset

## Capabilities

### New Capabilities
- `device-info/os-properties`: Operating system name and version properties with persistent storage across reboots

### Modified Capabilities
<!-- No existing capabilities are being modified -->

## Impact

- **Affected Files**:
  - External Thunder interfaces repository - Add osName/osVersion getter/setter methods to Exchange::IDeviceInfo interface
  - `plugin/DeviceInfoImplementation.h` - Implement osName/osVersion interface method overrides
  - `plugin/DeviceInfoImplementation.cpp` - Implement persistence logic using `/opt/persistent/osdetails.info`
  
- **No Changes Required**:
  - `plugin/DeviceInfo.h` and `plugin/DeviceInfo.cpp` - JSON-RPC registration is automatic via existing Exchange::JDeviceInfo::Register() call
  
- **New Dependencies**: 
  - Updated Thunder interfaces version (with new IDeviceInfo methods)
  - File I/O operations for `/opt/persistent/osdetails.info` (read/write key-value pairs)

- **API Impact**: Four new JSON-RPC methods (osName get/set, osVersion get/set) automatically exposed via DeviceInfo plugin

- **Testing**: New unit tests required for L1 and L2 test suites to verify persistence, reboot survival, and factory reset behavior
