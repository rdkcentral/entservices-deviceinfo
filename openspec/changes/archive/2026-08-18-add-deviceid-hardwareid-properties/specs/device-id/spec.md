## ADDED Requirements

### Requirement: deviceId property returns a stable alphanumeric device identifier
The DeviceInfo plugin SHALL expose a read-only `deviceId` property. Its value SHALL be determined as follows:
1. Retrieve `mfrSERIALIZED_TYPE_SERIALNUMBER` from the MFR library.
2. If the retrieved value contains at least one non-digit character (i.e., it is alphanumeric), the plugin SHALL return that value as `deviceId`.
3. If the retrieved value is entirely numeric, the plugin SHALL retrieve `mfrSERIALIZED_TYPE_MANUFACTURING_SERIALNUMBER` from the MFR library and return that value as `deviceId`.
4. If both MFR calls fail, the plugin SHALL return an empty string for `deviceId` and SHALL still return `Core::ERROR_NONE`.
The resolved value SHALL be cached in memory for the lifetime of the plugin instance so that subsequent calls do not invoke the MFR library again.

#### Scenario: Alphanumeric serial number is returned directly
- **WHEN** `mfrSERIALIZED_TYPE_SERIALNUMBER` returns an alphanumeric value (e.g., `EB21163216C000024`)
- **THEN** `deviceId` SHALL equal that alphanumeric serial number

#### Scenario: Numeric-only serial number triggers manufacturing serial number lookup
- **WHEN** `mfrSERIALIZED_TYPE_SERIALNUMBER` returns a purely numeric value (e.g., `84725041828384`)
- **AND** `mfrSERIALIZED_TYPE_MANUFACTURING_SERIALNUMBER` returns an alphanumeric value (e.g., `IP09SK925314001D0`)
- **THEN** `deviceId` SHALL equal the manufacturing serial number value

#### Scenario: Manufacturing serial number unavailable for numeric serial
- **WHEN** `mfrSERIALIZED_TYPE_SERIALNUMBER` returns a purely numeric value
- **AND** `mfrSERIALIZED_TYPE_MANUFACTURING_SERIALNUMBER` call fails
- **THEN** `deviceId` SHALL be an empty string
- **AND** the property call SHALL return `Core::ERROR_NONE`

#### Scenario: Cached value is returned on subsequent calls
- **WHEN** `deviceId` has been successfully resolved on a prior call
- **THEN** subsequent calls SHALL return the same cached value without invoking the MFR library
