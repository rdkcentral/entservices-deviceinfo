## ADDED Requirements

### Requirement: hardwareId property returns the first 6 characters of deviceId
The DeviceInfo plugin SHALL expose a read-only `hardwareId` property. Its value SHALL be the first 6 characters of the `deviceId` value. The plugin SHALL reuse the cached `deviceId` to derive `hardwareId` without invoking the MFR library independently. If `deviceId` is shorter than 6 characters, `hardwareId` SHALL equal the full `deviceId` value.

#### Scenario: hardwareId is the first 6 characters of an alphanumeric deviceId
- **WHEN** `deviceId` resolves to `EB21163216C000024`
- **THEN** `hardwareId` SHALL equal `EB2116`

#### Scenario: hardwareId is the first 6 characters of a HWID-composed deviceId
- **WHEN** `deviceId` is composed from HWID `32E304` and numeric serial `84725041828384` (yielding `32E3040000418283`)
- **THEN** `hardwareId` SHALL equal `32E304`

#### Scenario: hardwareId is the first 6 characters of the raw serial fallback
- **WHEN** both HWID and manufacturing serial number calls fail
- **AND** `deviceId` falls back to raw serial number `84725041828384`
- **THEN** `hardwareId` SHALL equal `847250`
- **AND** the property call SHALL return `Core::ERROR_NONE`

#### Scenario: hardwareId equals deviceId when deviceId is shorter than 6 characters
- **WHEN** `deviceId` resolves to a value with fewer than 6 characters (e.g., `AB12`)
- **THEN** `hardwareId` SHALL equal `AB12`
