## ADDED Requirements

### Requirement: deviceId property returns a stable alphanumeric device identifier
The DeviceInfo plugin SHALL expose a read-only `deviceId` property. Its value SHALL be determined as follows:
1. Retrieve `mfrSERIALIZED_TYPE_SERIALNUMBER` from the MFR library.
2. If the retrieved value contains at least one non-digit character (i.e., it is alphanumeric), the plugin SHALL return that value as `deviceId`.
3. If the retrieved value is entirely numeric, the plugin SHALL compose `deviceId` from the hardware identifier: retrieve `mfrSERIALIZED_TYPE_HWID` and compose `deviceId` as `<HWID> + "000" + serialNumber.substr(5, 7)`. For example, serial `84725041828384` with HWID `32E304` yields `deviceId` `32E3040000418283`.
4. If `mfrSERIALIZED_TYPE_HWID` is unavailable, the plugin SHALL fall back to `mfrSERIALIZED_TYPE_MANUFACTURING_SERIALNUMBER`.
5. If both HWID and Manufacturing Serial Number calls fail, the plugin SHALL use the raw `serialNumber` as `deviceId` and SHALL return `Core::ERROR_NONE`.
The resolved value SHALL be cached in memory for the lifetime of the plugin instance so that subsequent calls do not invoke the MFR library again.

#### Scenario: Alphanumeric serial number is returned directly
- **WHEN** `mfrSERIALIZED_TYPE_SERIALNUMBER` returns an alphanumeric value (e.g., `EB21163216C000024`)
- **THEN** `deviceId` SHALL equal that alphanumeric serial number

#### Scenario: Numeric-only serial number composes deviceId from HWID
- **WHEN** `mfrSERIALIZED_TYPE_SERIALNUMBER` returns a purely numeric value (e.g., `84725041828384`)
- **AND** `mfrSERIALIZED_TYPE_HWID` returns a value (e.g., `32E304`)
- **THEN** `deviceId` SHALL equal `<HWID> + "000" + serialNumber.substr(5, 7)` (e.g., `32E3040000418283`)

#### Scenario: HWID unavailable — fallback to manufacturing serial number
- **WHEN** `mfrSERIALIZED_TYPE_SERIALNUMBER` returns a purely numeric value
- **AND** `mfrSERIALIZED_TYPE_HWID` call fails
- **AND** `mfrSERIALIZED_TYPE_MANUFACTURING_SERIALNUMBER` returns a value
- **THEN** `deviceId` SHALL equal the manufacturing serial number value

#### Scenario: Both HWID and manufacturing serial number unavailable — fallback to raw serial
- **WHEN** `mfrSERIALIZED_TYPE_SERIALNUMBER` returns a purely numeric value (e.g., `84725041828384`)
- **AND** both `mfrSERIALIZED_TYPE_HWID` and `mfrSERIALIZED_TYPE_MANUFACTURING_SERIALNUMBER` calls fail
- **THEN** `deviceId` SHALL equal the raw serial number (e.g., `84725041828384`)
- **AND** the property call SHALL return `Core::ERROR_NONE`

#### Scenario: Cached value is returned on subsequent calls
- **WHEN** `deviceId` has been successfully resolved on a prior call
- **THEN** subsequent calls SHALL return the same cached value without invoking the MFR library
