# DeviceInfo Plugin - Product Documentation

## Product Overview

The DeviceInfo plugin is a critical infrastructure component for RDK (Reference Design Kit) devices, providing a unified JSON-RPC interface for querying device hardware characteristics, capabilities, and configuration. It serves as the authoritative source for device identification, audio/video capabilities discovery, and network configuration retrieval within the RDK ecosystem.

## Key Features

### 1. **Device Identification**
- **Hardware Identifiers**: Serial number, SKU, model, make, brand, and chipset information
- **Platform Details**: SoC name, distributor ID, device type classification
- **Version Information**: Firmware version, release version, and build details
- **System Information**: CPU architecture, total memory, uptime statistics

### 2. **Audio Capabilities Discovery**
- **Codec Support Detection**: Automatic detection of supported audio codecs per audio port
  - PCM (various bit depths and sample rates)
  - Compressed formats (AC3, E-AC3, AAC, DTS, Dolby Atmos)
- **MS12 Audio Processing**: MS12 capabilities enumeration including:
  - Supported audio processing profiles
  - Dolby Volume, Dialogue Enhancement, Intelligent EQ
  - Bass Enhancement, Surround Virtualizer capabilities
- **Port Enumeration**: Discovery of available audio output ports (HDMI, SPDIF, Headphone, Speaker)

### 3. **Video Capabilities Discovery**
- **Display Support**: Enumeration of connected video displays and output ports
- **EDID Information**: Host EDID reading for display compatibility detection
- **Resolution Support**: 
  - Default resolution configuration per display
  - Full list of supported resolutions (SD, HD, FHD, 4K, 8K)
- **HDCP Protection**: Detection of supported HDCP versions (1.4, 2.2) per display

### 4. **Network Configuration**
- **Hardware Addresses**: 
  - Ethernet MAC address
  - STB (Set-Top Box) MAC address
  - WiFi MAC address
- **IP Addressing**: Current STB IP address
- **Multi-Interface Support**: Comprehensive addressing information for all network interfaces

## Target Use Cases

### Application Development
- **Device Fingerprinting**: Applications can identify device capabilities to adapt content delivery
- **Capability-Based Streaming**: Video streaming apps query video capabilities to select appropriate codec and resolution
- **Audio Format Selection**: Music and media apps detect supported audio formats for optimal playback
- **Configuration Management**: Device management applications retrieve comprehensive device profiles

### System Integration
- **Provisioning Systems**: Automated device registration using serial numbers and MAC addresses
- **Analytics and Telemetry**: Device identification for usage tracking and diagnostics
- **Feature Gating**: Enable/disable features based on hardware capabilities
- **Compatibility Checks**: Verify device meets minimum requirements for content or applications

### Troubleshooting and Support
- **Remote Diagnostics**: Support teams query device information for troubleshooting
- **Capability Validation**: Verify hardware features match expected configuration
- **Version Tracking**: Monitor firmware versions across device fleet

## API Capabilities

### JSON-RPC Interface
The plugin exposes a comprehensive JSON-RPC 2.0 API with the following method categories:

#### Device Information Methods
- `systeminfo` - Complete system information (architecture, memory, uptime)
- `make` / `model` / `serialnumber` - Basic device identification
- `firmwareversion` / `releaseversion` - Software version information
- `chipset` / `socname` - Hardware platform details
- `devicetype` / `brand` / `distributorid` - Device classification

#### Audio Capability Methods
- `audiocapabilities` - Query supported codecs for a specific audio port
- `ms12capabilities` - Query MS12 audio processing capabilities
- `supportedms12audioprofiles` - List available MS12 profiles
- `supportedaudioports` - Enumerate all audio output ports

#### Video Capability Methods
- `supportedvideodisplays` - List all video displays
- `hostedid` - Retrieve EDID from connected display
- `defaultresolution` - Get default resolution for a display
- `supportedresolutions` - List all supported resolutions
- `supportedhdcp` - Query HDCP version support

#### Network Information Methods
- `addresses` - Comprehensive network interface information
- `ethmac` / `estbmac` / `wifimac` - Individual MAC address queries
- `estbip` - Current IP address

### Integration Benefits

#### For Application Developers
- **Single Source of Truth**: Eliminates need for multiple device information APIs
- **Standardized Interface**: Consistent JSON-RPC access across all RDK platforms
- **Asynchronous Support**: Non-blocking queries with event notifications
- **Type Safety**: Strongly-typed interfaces with COM-RPC for native code integration

#### For Platform Integrators
- **Hardware Abstraction**: Shields applications from platform-specific HAL details
- **Extensibility**: Easy to add new capabilities without breaking existing clients
- **Configuration Flexibility**: Plugin configuration supports platform-specific customization
- **Testing Support**: Comprehensive L1/L2 test suites for validation

## Performance Characteristics

### Response Times
- **Device Information Queries**: < 10ms (cached data)
- **Audio/Video Capabilities**: < 50ms (HAL queries)
- **EDID Reading**: < 100ms (I2C communication)
- **Network Information**: < 5ms (system calls)

### Resource Usage
- **Memory Footprint**: ~2MB resident memory
- **CPU Usage**: Minimal (< 1% during queries)
- **Startup Time**: < 100ms initialization

### Scalability
- **Concurrent Requests**: Thread-safe, supports multiple simultaneous clients
- **Query Frequency**: No rate limiting, suitable for periodic polling
- **Event Notifications**: Supports push notifications for configuration changes

## Reliability and Quality

### Error Handling
- Graceful failure modes with detailed error codes
- Automatic retry mechanisms for transient hardware failures
- Comprehensive logging for diagnostics
- Fallback values for unavailable information

### Testing Coverage
- **L1 Tests**: Unit tests covering all interface methods
- **L2 Tests**: Integration tests with hardware abstraction layer
- **Regression Tests**: Automated testing on each code change
- **Valgrind Analysis**: Memory leak and error detection

### Platform Support
- Compatible with all WPEFramework R4.x versions
- Supports Thunder's out-of-process and in-process execution modes
- Cross-platform: Linux-based RDK devices (STB, broadband gateways, smart displays)

## Integration Requirements

### Prerequisites
- WPEFramework (Thunder) R4.4.1 or later
- RDK Device Settings HAL (dsHAL) available
- IARM Bus daemon running
- RFC configuration service (optional, for advanced features)

### Configuration
- Plugin configuration via JSON file (`DeviceInfo.json`)
- Platform-specific settings via CMake build options
- Runtime configuration through IConfiguration interface

### Deployment
- Shared library: `libWPEFrameworkDeviceInfo.so`
- Installation path: `/usr/lib/wpeframework/plugins/`
- Configuration: `/etc/WPEFramework/plugins/DeviceInfo.json`
- Automatic activation on Thunder startup (configurable)

## Future Enhancements
- Extended capability reporting (GPU, codec support)
- Dynamic capability updates with event notifications
- Performance metrics and statistics APIs
- Enhanced security features (capability-based access control)
