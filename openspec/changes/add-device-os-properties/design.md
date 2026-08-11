## Context

The DeviceInfo plugin currently provides read-only device information through the Thunder framework's JSON-RPC interface. Properties are exposed via the Exchange::IDeviceInfo interface and registered through Exchange::JDeviceInfo. The plugin follows a two-layer architecture:

1. **DeviceInfo.cpp/h**: Thunder plugin entry point that handles plugin lifecycle and JSON-RPC registration
2. **DeviceInfoImplementation.cpp/h**: Core implementation that retrieves device information from underlying platform APIs

Currently, all properties are read-only and sourced from system APIs or configuration. No properties support persistence or write operations.

See proposal.md for motivation.

## Goals / Non-Goals

**Goals:**
- Add two new read-write properties (osName, osVersion) to the existing DeviceInfo plugin
- Implement file-based persistence using key-value format
- Ensure thread-safe file access for concurrent get/set operations
- Maintain backward compatibility with existing DeviceInfo APIs

**Non-Goals:**
- Modifying existing read-only properties
- Implementing a generalized persistence framework for other properties
- Supporting persistence mechanisms other than file-based storage
- Factory reset implementation (out of scope - handled by platform)

## Decisions

### Decision 1: Extend IDeviceInfo interface vs. Create new interface
**Chosen:** Extend Exchange::IDeviceInfo interface (external Thunder interfaces)

**Rationale:**
- osName and osVersion are logically part of device information
- Keeps all device-related properties in a single interface
- Avoids additional complexity of managing multiple interfaces
- Follows existing pattern for device properties
- Leverages automatic JSON-RPC registration via Exchange::JDeviceInfo::Register()

**Implementation Note:**
The Exchange::IDeviceInfo interface is defined in the external Thunder interfaces repository, not in this repo. This change requires:
1. First, update Thunder interfaces to add `osName()`, `setOsName(string)`, `osVersion()`, `setOsVersion(string)` methods to Exchange::IDeviceInfo
2. Then, implement those methods in DeviceInfoImplementation.h/cpp in this repo
3. No changes needed to DeviceInfo.h/cpp - existing JSON-RPC registration automatically exposes the new interface methods

**Alternatives Considered:**
- Create new Exchange::IDeviceOSInfo interface: Would fragment device information across multiple interfaces, adding unnecessary complexity
- Manual JSON-RPC registration without interface extension: Would bypass the COM interface pattern, inconsistent with existing properties

### Decision 2: Persistence file format
**Chosen:** Simple key-value pairs with newline separation

Format:
```
osname=RDK
osversion=8.1
```

**Rationale:**
- Simple to parse and write
- Human-readable for debugging
- No external dependencies (JSON, INI parsers)
- Sufficient for two string properties

**Alternatives Considered:**
- JSON format: Overkill for two properties, adds parsing complexity
- Binary format: Not human-readable, no performance benefit for this use case

### Decision 3: File location
**Chosen:** `/opt/persistent/osdetails.info`

**Rationale:**
- Specified by stakeholder requirement
- `/opt/persistent/` directory survives reboots but is cleared on factory reset
- Follows RDK platform conventions

### Decision 4: Thread safety approach
**Chosen:** File-level locking using flock() for read/write operations

**Rationale:**
- Protects against concurrent access from multiple processes
- Standard POSIX mechanism
- Minimal performance impact for infrequent operations

**Alternatives Considered:**
- In-memory caching with mutex: Doesn't protect against multi-process access
- No locking: Risk of file corruption during concurrent writes

### Decision 5: Error handling for missing file
**Chosen:** Return empty string if file doesn't exist, create on first write

**Rationale:**
- Matches stakeholder requirement
- Graceful degradation - no errors for fresh devices
- Simplifies initialization - no need to pre-create file

## Risks / Trade-offs

**Risk:** File I/O failures (disk full, permission issues)
→ **Mitigation:** Log errors, return last known value from memory cache on read failures, return error code on write failures

**Risk:** File corruption from partial writes
→ **Mitigation:** Write to temporary file and atomic rename; use file locking

**Risk:** Performance impact of file I/O on every get operation
→ **Mitigation:** Implement in-memory cache, refresh on set and at initialization; file I/O only on set operations

**Trade-off:** Simple key-value format vs. extensibility
→ If more properties need persistence in future, may need to refactor to structured format (JSON). Current approach optimizes for immediate requirements.

**Trade-off:** File-based persistence vs. database
→ File-based is simpler but doesn't scale to many properties. Acceptable for two properties; would need re-evaluation if persistence requirements grow.

## Implementation Notes

### Property Registration
Both properties will be automatically registered for JSON-RPC access via the existing `Exchange::JDeviceInfo::Register(*this, _deviceInfo)` call in DeviceInfo.cpp. No additional registration code needed - the Thunder framework automatically generates JSON-RPC bindings for all methods defined in the Exchange::IDeviceInfo interface.

### File Access Pattern
1. **Initialization**: Read file into memory cache
2. **Get operation**: Return from memory cache
3. **Set operation**: Update memory cache, then write to file
4. **File write**: Lock → Write to temp file → Rename → Unlock

### Key-Value Parsing
- Read entire file into buffer
- Split by newlines
- For each line: split on first '=' character
- Trim whitespace from keys and values
- Store in memory map

### Backward Compatibility
No impact - adding new properties doesn't affect existing APIs. Devices without the file will return empty strings, which is valid behavior.
