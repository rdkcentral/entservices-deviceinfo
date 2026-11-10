# DeviceInfo Plugin Architecture

## Overview

The DeviceInfo plugin is a WPEFramework (Thunder) plugin that provides comprehensive device information and capabilities to RDK applications. It exposes device hardware details, network configuration, audio/video capabilities, and system information through a standardized JSON-RPC interface.

## System Architecture

### Component Structure

```
┌─────────────────────────────────────────────────────────────┐
│                    WPEFramework Core                         │
│                 (Thunder Plugin Framework)                   │
└────────────────────────┬────────────────────────────────────┘
                         │
                         │ Plugin Host Interface
                         │
┌────────────────────────▼────────────────────────────────────┐
│                  DeviceInfo Plugin                           │
│  ┌──────────────────────────────────────────────────────┐  │
│  │           DeviceInfo (Main Plugin Class)             │  │
│  │  - PluginHost::IPlugin                               │  │
│  │  - PluginHost::JSONRPC                               │  │
│  └─────┬────────────────────────────────────────────────┘  │
│        │ Aggregates                                         │
│  ┌─────▼──────────────┐  ┌──────────────────────────────┐  │
│  │ DeviceInfoImpl     │  │ DeviceAudioCapabilities      │  │
│  │ - IDeviceInfo      │  │ - IAudioCapabilities         │  │
│  │ - IConfiguration   │  │                              │  │
│  └─────┬──────────────┘  └──────────────────────────────┘  │
│        │                                                     │
│  ┌─────▼──────────────────────────────────────────────┐    │
│  │         DeviceVideoCapabilities                     │    │
│  │         - IVideoCapabilities                        │    │
│  └─────────────────────────────────────────────────────┘    │
└────────────────────────┬────────────────────────────────────┘
                         │
            ┌────────────┼────────────┐
            │            │            │
      ┌─────▼────┐  ┌───▼────┐  ┌───▼──────┐
      │   RFC    │  │   DS   │  │  IARM    │
      │   API    │  │ Plugin │  │   Bus    │
      └──────────┘  └────────┘  └──────────┘
         Config      Device      Inter-Process
         Management  Settings    Communication
```

DeviceInfo accesses the DeviceSettings Thunder plugin over COM-RPC via DSHelper, rather than calling the Device Settings HAL directly.

### Core Components

#### 1. **DeviceInfo Plugin (Main Entry Point)**
- **Responsibility**: Plugin lifecycle management, interface aggregation, JSON-RPC endpoint registration
- **Key Interfaces**: 
  - `PluginHost::IPlugin` - Standard WPEFramework plugin interface
  - `PluginHost::JSONRPC` - JSON-RPC method dispatcher
- **Aggregated Services**: IDeviceInfo, IDeviceAudioCapabilities, IDeviceVideoCapabilities

#### 2. **DeviceInfoImplementation**
- **Responsibility**: Core device information retrieval
- **Data Provided**:
  - Hardware identifiers (serial number, SKU, make, model, chipset)
  - Firmware and release versions
  - System information (architecture, uptime, memory)
  - Network addresses (Ethernet MAC, STB MAC, WiFi MAC, IP addresses)
  - Supported audio port enumeration
- **Dependencies**: RFC API (configuration), DeviceSettings Thunder plugin via COM-RPC, IARM Bus

#### 3. **DeviceAudioCapabilities**
- **Responsibility**: Audio hardware capability reporting
- **Data Provided**:
  - Supported audio codecs per port (PCM, AAC, Dolby formats)
  - MS12 audio processing capabilities
  - Supported MS12 audio profiles
- **Dependencies**: DeviceSettings Thunder plugin via COM-RPC (audio subsystem)

#### 4. **DeviceVideoCapabilities**
- **Responsibility**: Video hardware capability reporting
- **Data Provided**:
  - Supported video displays and output ports
  - Host EDID information
  - Default and supported resolutions per display
  - HDCP version support
- **Dependencies**: DeviceSettings Thunder plugin via COM-RPC (video and host subsystems)

## Data Flow

### Initialization Sequence
1. WPEFramework loads DeviceInfo plugin and calls `Initialize()`
2. Plugin instantiates implementation objects (DeviceInfoImpl, AudioCapabilities, VideoCapabilities)
3. DeviceInfoImplementation initializes the IARM Bus connection; configuration of all three implementations opens DeviceSettings COM-RPC links via DSHelper. DeviceSettings configuration and port handles are loaded lazily on first access.
4. Plugin registers JSON-RPC methods and interfaces with framework
5. Plugin becomes available for client requests

### Request Processing Flow
```
Client Application
      │
      │ JSON-RPC Request
      ▼
WPEFramework JSONRPC Dispatcher
      │
      │ Route to Plugin
      ▼
DeviceInfo Plugin Interface
      │
      │ Delegate to Implementation
      ▼
Implementation Layer (DeviceInfoImpl/Audio/Video)
      │
      ├─► RFC API (for configuration data)
      ├─► DeviceSettings Thunder plugin via DSHelper/COM-RPC (for hardware capabilities)
      └─► IARM Bus (for system information)
      │
      │ Aggregate Response
      ▼
JSON Response to Client
```

## Integration Points

### WPEFramework Integration
- **Plugin Discovery**: Registered via CMake configuration and `.conf.in` files
- **Communication**: COM-RPC for out-of-process communication, in-process for local calls
- **Configuration**: JSON-based plugin configuration via WPEFramework configuration system

### RDK Platform Integration
- **RFC (Remote Feature Control)**: Retrieves device-specific configuration parameters
- **DeviceSettings Thunder Plugin**: Provides audio/video configuration and capabilities through COM-RPC interfaces
- **IARM Bus**: Inter-process communication for system service coordination

### Helper Utilities
- **DeviceSettingsInterface.h / DSHelper**: DeviceSettings COM-RPC link management, lazy configuration caching and port handle lookup
- **UtilsIarm.h**: IARM Bus initialization and connection management
- **UtilsLogging.h**: Standardized logging macros for debugging and diagnostics

## Dependencies

### Build Dependencies
- **WPEFramework Core**: Plugin framework and COM-RPC infrastructure
- **entservices-apis**: Interface definitions (IDeviceInfo, IAudioCapabilities, IVideoCapabilities)
- **RFC Library**: Configuration management
- **entservices-helpers**: DeviceSettings client helper and interface utilities
- **IARM Bus Library**: Inter-process communication

### Runtime Dependencies
- Thunder process must be running
- DeviceSettings Thunder plugin must be available for audio/video queries
- IARM Bus daemon must be active

## Thread Safety and Concurrency
- All interface methods are designed to be thread-safe
- IARM Bus operations use internal locking mechanisms
- DSHelper caches DeviceSettings configuration and port handles; these caches are cleared when DeviceSettings deactivates
- Plugin lifecycle operations (Initialize/Deinitialize) are serialized by framework

## Error Handling
- Returns `Core::hresult` error codes for all interface methods
- Logs errors via UtilsLogging macros (LOGERR, LOGWARN, LOGINFO)
- Gracefully handles hardware access failures with error responses
- Reports success/failure status via output parameters

## Performance Considerations
- Device information queries are typically cached by hardware layer
- Minimal processing overhead - mostly data marshaling
- EDID parsing and resolution queries may involve I2C communication (millisecond latency)
- Network address queries require system calls (sub-millisecond)

## Security Considerations
- Plugin runs with system privileges (Thunder process context)
- No authentication/authorization - relies on framework's security model
- Exposed device information may be sensitive (MAC addresses, serial numbers)
- No input validation required for read-only operations
