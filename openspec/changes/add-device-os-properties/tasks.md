## 1. Thunder Interface Updates (External Dependency)

- [x] 1.1 Update Thunder interfaces repository to add osName() getter method to Exchange::IDeviceInfo - DEFERRED (external repo)
- [x] 1.2 Update Thunder interfaces repository to add setOsName(const string&) setter method to Exchange::IDeviceInfo - DEFERRED (external repo)
- [x] 1.3 Update Thunder interfaces repository to add osVersion() getter method to Exchange::IDeviceInfo - DEFERRED (external repo)
- [x] 1.4 Update Thunder interfaces repository to add setOsVersion(const string&) setter method to Exchange::IDeviceInfo - DEFERRED (external repo)
- [x] 1.5 Update Thunder interfaces dependency version in this repo's build configuration - DEFERRED (pending external interface)

## 2. DeviceInfoImplementation Interface Updates

- [x] 2.1 Implement osName() getter method override in DeviceInfoImplementation.h
- [x] 2.2 Implement setOsName(const string&) setter method override in DeviceInfoImplementation.h
- [x] 2.3 Implement osVersion() getter method override in DeviceInfoImplementation.h
- [x] 2.4 Implement setOsVersion(const string&) setter method override in DeviceInfoImplementation.h
- [x] 2.5 Add private member variables for osName and osVersion cache in DeviceInfoImplementation class

## 3. Persistence Layer Implementation

- [x] 3.1 Create persistence file utility class/methods for reading key-value pairs from /opt/persistent/osdetails.info
- [x] 3.2 Implement key-value file parsing logic (split by newlines, parse key=value format)
- [x] 3.3 Implement file writing logic with atomic rename (write to temp file, then rename)
- [x] 3.4 Add file locking mechanism using flock() for thread-safe access
- [x] 3.5 Implement error handling for file I/O operations (disk full, permissions, corruption)

## 4. Property Implementation

- [x] 4.1 Implement osName() getter in DeviceInfoImplementation.cpp (return from memory cache)
- [x] 4.2 Implement setOsName(const string&) setter in DeviceInfoImplementation.cpp (update cache, persist to file)
- [x] 4.3 Implement osVersion() getter in DeviceInfoImplementation.cpp (return from memory cache)
- [x] 4.4 Implement setOsVersion(const string&) setter in DeviceInfoImplementation.cpp (update cache, persist to file)
- [x] 4.5 Add initialization logic in Configure() to load persisted values from file into memory cache
- [x] 4.6 Handle empty string return when persistence file does not exist
- [x] 4.7 Add logging for property get/set operations

## 5. Testing - L1 Unit Tests

Note: JSON-RPC registration happens automatically via existing Exchange::JDeviceInfo::Register() call - no additional registration code needed

- [x] 5.1 Create test file for osName/osVersion properties (or extend existing test_DeviceInfo.cpp)
- [x] 5.2 Add test case: Get osName when file doesn't exist (expect empty string)
- [x] 5.3 Add test case: Get osVersion when file doesn't exist (expect empty string)
- [x] 5.4 Add test case: Set osName value and verify persistence
- [x] 5.5 Add test case: Set osVersion value and verify persistence
- [x] 5.6 Add test case: Verify both properties persist independently in same file
- [x] 5.7 Add test case: Verify file is created on first set operation
- [x] 5.8 Add test case: Verify file update on subsequent set operations
- [x] 5.9 Add test case: Mock file I/O errors and verify error handling
- [x] 5.10 Add test case: Verify key-value parsing handles whitespace correctly

## 6. Testing - L2 Integration Tests

- [ ] 6.1 Add L2 test case for osName get operation (empty file scenario)
- [ ] 6.2 Add L2 test case for osName set operation
- [ ] 6.3 Add L2 test case for osVersion get operation (empty file scenario)
- [ ] 6.4 Add L2 test case for osVersion set operation
- [ ] 6.5 Add L2 test case for persistence across plugin restart (simulate reboot)
- [ ] 6.6 Add L2 test case for JSON-RPC interface validation

## 7. Documentation and Configuration

- [x] 7.1 Update plugin/CHANGELOG.md with new osName and osVersion properties
- [x] 7.2 Add API documentation comments for new properties in header files
- [x] 7.3 Verify no changes needed to DeviceInfo.config (properties use default configuration)
- [x] 7.4 Add logging documentation for new file I/O operations

## 8. Build and Validation

- [ ] 8.1 Build the plugin and verify no compilation errors
- [ ] 8.2 Run L1 unit tests and verify all tests pass
- [ ] 8.3 Run L2 integration tests and verify all tests pass
- [ ] 8.4 Manual testing: Verify properties work via JSON-RPC interface
- [ ] 8.5 Manual testing: Verify file is created at /opt/persistent/osdetails.info
- [ ] 8.6 Manual testing: Verify persistence survives plugin restart
- [ ] 8.7 Run static analysis tools (if applicable) and fix any issues
