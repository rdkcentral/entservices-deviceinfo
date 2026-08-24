## ADDED Requirements

### Requirement: hardwareId property returns the first 6 characters of deviceId
The DeviceInfo plugin SHALL expose a read-only `hardwareId` property. Its value SHALL be the first 6 characters of the `deviceId` value. The plugin SHALL reuse the cached `deviceId` to derive `hardwareId` without invoking the MFR library independently. If `deviceId` is shorter than 6 characters, `hardwareId` SHALL equal the full `deviceId` value.

#### Scenario: hardwareId is the first 6 characters of an alphanumeric deviceId
- **WHEN** `deviceId` resolves to `EB21163216C000024`
- **THEN** `hardwareId` SHALL equal `EB2116`

#### Scenario: hardwareId is the first 6 characters of a manufacturing serial number deviceId
- **WHEN** `deviceId` resolves to `IP09SK925314001D0`
- **THEN** `hardwareId` SHALL equal `IP09SK`

#### Scenario: hardwareId is empty when deviceId is empty
- **WHEN** `deviceId` resolves to an empty string
- **THEN** `hardwareId` SHALL be an empty string
- **AND** the property call SHALL return `Core::ERROR_NONE`

#### Scenario: hardwareId equals deviceId when deviceId is shorter than 6 characters
- **WHEN** `deviceId` resolves to a value with fewer than 6 characters (e.g., `AB12`)
- **THEN** `hardwareId` SHALL equal `AB12`
