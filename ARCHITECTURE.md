# PlayerInfo Plugin Architecture

## Overview

The PlayerInfo plugin is a Thunder (WPEFramework) plugin that provides comprehensive information about the media playback capabilities of a device. It exposes codec information, audio/video properties, and Dolby audio features through a JSON-RPC interface, enabling applications to query device capabilities before attempting media playback.

## System Architecture

### Component Hierarchy

```
┌─────────────────────────────────────────────────────────┐
│              Thunder Framework (WPEFramework)            │
│                   Plugin Host Environment                │
└──────────────────┬──────────────────────────────────────┘
                   │
         ┌─────────▼──────────┐
         │  PlayerInfo Plugin │
         │   (JSON-RPC API)   │
         └─────────┬──────────┘
                   │
      ┌────────────┴────────────┐
      │                         │
┌─────▼─────────┐      ┌───────▼──────────┐
│ IPlayerProps  │      │  Dolby IOutput   │
│   Interface   │      │    Interface     │
└─────┬─────────┘      └───────┬──────────┘
      │                        │
      └────────┬───────────────┘
               │
    ┌──────────▼───────────┐
    │ Platform             │
    │ Implementation Layer │
    └──────────┬───────────┘
               │
    ┌──────────┴───────────┐
    │                      │
┌───▼──────────┐  ┌────────▼─────────┐
│  GStreamer   │  │ Device Settings  │
│ Based Impl   │  │   (IARM/DS)      │
└──────────────┘  └──────────────────┘
```

### Core Components

#### 1. **PlayerInfo Plugin Class**
- **Purpose**: Main plugin entry point and orchestrator
- **Responsibilities**:
  - Plugin lifecycle management (Initialize, Deinitialize)
  - JSON-RPC interface registration and handling
  - Connection management with platform implementation
  - Event notification routing (Dolby audio mode changes)
- **Key Interfaces**: `IPlugin`, `IWeb`, `JSONRPC`

#### 2. **Platform Implementation Layer**
Two platform-specific implementations provide hardware abstraction:

**a) GStreamer-Based Implementation**
- Uses GStreamer multimedia framework for codec discovery
- Queries GStreamer registry for available decoders and parsers
- Supports both audio and video codec enumeration
- Platform-agnostic approach suitable for generic Linux systems

**b) Device Settings Implementation (IARM/DS)**
- Leverages RDK Device Settings HAL and IARM Bus
- Direct hardware capability queries through device-specific APIs
- Provides audio output port configuration and properties
- Supports Dolby MS12 audio processing capabilities
- Tightly integrated with STB/TV hardware subsystems

#### 3. **Exchange Interfaces**
- **IPlayerProperties**: Primary interface for codec queries
  - `AudioCodecs()`: Returns iterator of supported audio codecs
  - `VideoCodecs()`: Returns iterator of supported video codecs
  - `Resolution()`: Provides current video resolution capabilities
- **IDolby::IOutput**: Dolby-specific audio interface
  - Audio mode enumeration and configuration
  - Event notifications for audio mode changes

## Data Flow

### Codec Query Sequence
```
Client Request
    ↓
JSON-RPC Handler (PlayerInfo)
    ↓
IPlayerProperties::AudioCodecs() / VideoCodecs()
    ↓
Platform Implementation
    ↓
┌──────────────────────┬────────────────────┐
│ GStreamer Path       │  Device Settings   │
│                      │      Path          │
│ 1. Query GST Registry│  1. IARM Connect   │
│ 2. Check Decoders    │  2. DS HAL Query   │
│ 3. Check Parsers     │  3. Audio Port Info│
│ 4. Build Codec List  │  4. Return Codecs  │
└──────────────────────┴────────────────────┘
    ↓
Codec Iterator
    ↓
JSON Response to Client
```

### Event Notification Flow
```
Hardware Event (Dolby Mode Change)
    ↓
Platform Implementation
    ↓
IOutput::INotification::AudioModeChanged()
    ↓
DolbyNotification Handler
    ↓
JSON-RPC Event Broadcast
    ↓
Registered Clients Notified
```

## Key Dependencies

### Build-Time Dependencies
- **Thunder (WPEFramework)**: Core plugin framework (v4.4+)
- **Thunder Interfaces**: Exchange interface definitions
- **GStreamer**: Multimedia framework (optional, for GStreamer impl)
- **Device Settings (DS)**: RDK HAL library (optional, for DS impl)
- **IARM Bus**: Inter-process communication (for DS impl)

### Runtime Dependencies
- **Thunder Core Services**: Process management and IPC
- **Device Settings Manager**: Audio/video subsystem manager (DS path)
- **GStreamer Plugins**: Codec plugins and parsers (GStreamer path)

## Configuration

The plugin supports compile-time and runtime configuration:

### CMake Build Options
- `USE_DEVICESETTINGS`: Enable Device Settings implementation
- `DOLBY_SUPPORT`: Enable Dolby audio features
- `RDK_SERVICE_L2_TEST`: Enable L2 test-specific registrations

### Runtime Configuration (JSON)
```json
{
  "autostart": true,
  "mode": "Off",
  "startuporder": ""
}
```

## Thread Safety & Concurrency

- JSON-RPC calls are serialized by Thunder framework
- Platform implementations use thread-safe APIs:
  - GStreamer: Registry queries are thread-safe
  - IARM Bus: Provides synchronous call guarantees
- Event notifications handled on Thunder's dispatcher thread

## Testing Strategy

### L1 Tests (Unit Tests)
- Mock-based testing of plugin interfaces
- Codec iterator validation
- JSON-RPC method verification
- Isolated component testing

### L2 Tests (Integration Tests)
- End-to-end JSON-RPC API validation
- Codec enumeration accuracy
- Platform implementation verification
- Cross-platform compatibility testing

## Extension Points

The architecture supports extension through:
1. Additional platform implementations (new hardware abstractions)
2. New codec types in Exchange interfaces
3. Enhanced Dolby audio features
4. Additional player property queries (HDR, resolution, etc.)

## Performance Considerations

- **Codec queries**: Cached at initialization for GStreamer path
- **IARM calls**: Synchronous but optimized for low latency
- **Memory footprint**: Minimal, primarily interface proxies
- **Startup time**: Sub-second initialization on typical hardware

## Security

- No direct file system or network access
- Capability queries only, no device control
- Thunder security model enforced (token validation)
- Read-only operations, no state modification
