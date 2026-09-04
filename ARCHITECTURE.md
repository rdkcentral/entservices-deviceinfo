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
      │   API    │  │  HAL   │  │   Bus    │
      └──────────┘  └────────┘  └──────────┘
         Config      Device      Inter-Process
         Management  Settings    Communication
```

**Note**: The diagram above shows the DS_IARM implementation path. When built with `USE_DEVICESETTING_PLUGIN=ON`, the "Device Settings HAL → libds → IARM" flow is replaced by "COM-RPC → DeviceSettings Thunder plugin" for audio/video capability queries.

### Implementation Backends

The plugin supports two implementation backends, selected at build-time via the `USE_DEVICESETTING_PLUGIN` CMake option:

- **DS_IARM (default)**: Uses libds and IARM Bus for device settings queries. This is the original implementation that directly accesses the Device Settings HAL library via IARM IPC.

- **DS_COMRPC**: Uses COM-RPC to communicate with the DeviceSettings Thunder plugin via entservices-helpers. Device capability information is obtained by calling DeviceSettings plugin methods through Thunder's COM-RPC interface rather than direct HAL access.

Both implementations provide identical JSON-RPC APIs to clients - the difference is purely in how device capability information is obtained internally.

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
- **Dependencies**:
  - DS_IARM: RFC API, Device Settings HAL (libds), IARM Bus
  - DS_COMRPC: RFC API, IARM Bus, DeviceSettings Thunder plugin (via COM-RPC)

#### 3. **DeviceAudioCapabilities**
- **Responsibility**: Audio hardware capability reporting
- **Data Provided**:
  - Supported audio codecs per port (PCM, AAC, Dolby formats)
  - MS12 audio processing capabilities
  - Supported MS12 audio profiles
- **Dependencies**:
  - DS_IARM: Device Settings HAL (audio subsystem via libds)
  - DS_COMRPC: DeviceSettings Thunder plugin (via COM-RPC)

#### 4. **DeviceVideoCapabilities**
- **Responsibility**: Video hardware capability reporting
- **Data Provided**:
  - Supported video displays and output ports
  - Host EDID information
  - Default and supported resolutions per display
  - HDCP version support
- **Dependencies**:
  - DS_IARM: Device Settings HAL (video subsystem via libds)
  - DS_COMRPC: DeviceSettings Thunder plugin (via COM-RPC)

## Data Flow

### Initialization Sequence
1. WPEFramework loads DeviceInfo plugin and calls `Initialize()`
2. Plugin instantiates implementation objects (DeviceInfoImpl, AudioCapabilities, VideoCapabilities)
3. Each implementation initializes its backend:
   - **DS_IARM**: IARM Bus connection for direct HAL access
   - **DS_COMRPC**: COM-RPC link to DeviceSettings Thunder plugin
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
      │
      ├─► DS_IARM: Device Settings HAL via libds
      │   DS_COMRPC: DeviceSettings Thunder plugin via COM-RPC
      │
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
- **Device Settings**:
  - DS_IARM: Low-level hardware abstraction via Device Settings HAL (libds)
  - DS_COMRPC: Thunder plugin interface via COM-RPC
- **IARM Bus**: Inter-process communication for system service coordination

### Helper Utilities
- **UtilsIarm.h**: IARM Bus initialization and connection management
- **UtilsLogging.h**: Standardized logging macros for debugging and diagnostics

## Dependencies

### Build Dependencies

**Common** (both implementations):
- **WPEFramework Core**: Plugin framework and COM-RPC infrastructure
- **entservices-apis**: Interface definitions (IDeviceInfo, IAudioCapabilities, IVideoCapabilities)
- **entservices-helpers**: Helper utilities and DeviceSettings COM-RPC interface wrappers
- **RFC Library**: Configuration management
- **IARM Bus Library**: Inter-process communication

**DS_IARM-specific** (`USE_DEVICESETTING_PLUGIN=OFF`, default):
- **Device Settings Library (libds)**: Hardware abstraction layer for direct HAL access

**DS_COMRPC-specific** (`USE_DEVICESETTING_PLUGIN=ON`):
- No additional build dependencies (uses DeviceSettings Thunder plugin at runtime)

### Build Configuration

The implementation backend is selected via the CMake option:

- **`USE_DEVICESETTING_PLUGIN=OFF` (default)**: Builds with DS_IARM implementation
- **`USE_DEVICESETTING_PLUGIN=ON`**: Builds with DS_COMRPC implementation

Example:
```bash
cmake -DUSE_DEVICESETTING_PLUGIN=ON ..
```

### Runtime Dependencies

**Common** (both implementations):
- Thunder process must be running
- IARM Bus daemon must be active

**DS_IARM-specific**:
- Device Settings service (dsHAL) must be available

**DS_COMRPC-specific**:
- DeviceSettings Thunder plugin must be loaded and active

## Thread Safety and Concurrency
- All interface methods are designed to be thread-safe
- IARM Bus operations use internal locking mechanisms
- No shared mutable state between concurrent requests
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
