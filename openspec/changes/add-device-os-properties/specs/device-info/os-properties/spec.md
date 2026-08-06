## Purpose

Provides persistent storage and retrieval of operating system name and version information for telemetry and customer support purposes across device reboots.

## ADDED Requirements

### Requirement: osName property support

The DeviceInfo plugin SHALL support a property named `osName` with both get and set operations, using string data type.

#### Scenario: Get osName when not previously set
- **WHEN** the device boots and Thunder framework & DeviceInfo plugin are running
- **AND** `osName` has never been set (persistence file does not exist)
- **THEN** a get operation on `DeviceInfo.osName` SHALL return an empty string `""`

#### Scenario: Set osName value
- **WHEN** a set operation is performed on `DeviceInfo.osName` with a valid string value
- **THEN** the value SHALL be persisted to `/opt/persistent/osdetails.info`
- **AND** subsequent get operations SHALL return the persisted value

#### Scenario: Get osName after reboot
- **WHEN** `osName` has been set to a value
- **AND** the device reboots
- **THEN** a get operation on `DeviceInfo.osName` SHALL return the previously persisted value

#### Scenario: osName cleared on factory reset
- **WHEN** a factory reset is performed on the device
- **THEN** the persisted `osName` value SHALL be wiped out
- **AND** subsequent get operations SHALL return an empty string `""`

### Requirement: osVersion property support

The DeviceInfo plugin SHALL support a property named `osVersion` with both get and set operations, using string data type.

#### Scenario: Get osVersion when not previously set
- **WHEN** the device boots and Thunder framework & DeviceInfo plugin are running
- **AND** `osVersion` has never been set (persistence file does not exist)
- **THEN** a get operation on `DeviceInfo.osVersion` SHALL return an empty string `""`

#### Scenario: Set osVersion value
- **WHEN** a set operation is performed on `DeviceInfo.osVersion` with a valid string value
- **THEN** the value SHALL be persisted to `/opt/persistent/osdetails.info`
- **AND** subsequent get operations SHALL return the persisted value

#### Scenario: Get osVersion after reboot
- **WHEN** `osVersion` has been set to a value
- **AND** the device reboots
- **THEN** a get operation on `DeviceInfo.osVersion` SHALL return the previously persisted value

#### Scenario: osVersion cleared on factory reset
- **WHEN** a factory reset is performed on the device
- **THEN** the persisted `osVersion` value SHALL be wiped out
- **AND** subsequent get operations SHALL return an empty string `""`

### Requirement: Persistent storage format

The OS properties SHALL be persisted using a key-value pair format in the file `/opt/persistent/osdetails.info`.

#### Scenario: First set operation creates file
- **WHEN** the persistence file `/opt/persistent/osdetails.info` does not exist
- **AND** the first set operation is performed on either `osName` or `osVersion`
- **THEN** the file SHALL be created with the appropriate key-value entry

#### Scenario: Subsequent set updates file
- **WHEN** the persistence file `/opt/persistent/osdetails.info` already exists
- **AND** a set operation is performed on either `osName` or `osVersion`
- **THEN** the corresponding entry in the file SHALL be updated with the new value

#### Scenario: Independent property persistence
- **WHEN** `osName` is set to a value
- **AND** `osVersion` is set to a different value
- **THEN** both values SHALL be persisted independently in the same file
- **AND** retrieving either property SHALL return its respective persisted value

## Covered Code

- External Thunder interfaces repository (Exchange::IDeviceInfo):
    - osName() getter method declaration
    - setOsName(const string&) setter method declaration
    - osVersion() getter method declaration
    - setOsVersion(const string&) setter method declaration
- plugin/DeviceInfoImplementation.h:
    - Override implementations of osName/osVersion interface methods
- plugin/DeviceInfoImplementation.cpp:
    - osName/osVersion getter/setter implementations
    - Persistence file I/O logic
    - Key-value parsing and writing for /opt/persistent/osdetails.info

Note: DeviceInfo.h and DeviceInfo.cpp require no modifications. JSON-RPC registration happens automatically via the existing Exchange::JDeviceInfo::Register() call.
