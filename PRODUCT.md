# PlayerInfo Plugin - Product Documentation

## Product Overview

The PlayerInfo plugin is a Thunder framework component that enables applications and services to discover and query media playback capabilities of RDK-powered devices. It provides real-time access to supported audio/video codecs, resolution information, and Dolby audio features, allowing intelligent content selection and optimal playback configuration.

## Key Features

### 1. **Codec Discovery**
- **Audio Codec Enumeration**: Comprehensive list of supported audio formats
  - Dolby formats (AC3, EAC3, AC4, Dolby Digital Plus, Dolby Atmos)
  - Standard formats (AAC, MP3, MPEG, VORBIS, OPUS, WAV, FLAC)
  - Advanced formats (DTS, WMA, DRA)
- **Video Codec Enumeration**: Complete video codec capabilities
  - Modern codecs (H.264, H.265/HEVC, VP8, VP9, AV1)
  - Legacy formats (MPEG-2, MPEG-4, H.263)
  - Professional formats (VC-1)

### 2. **Dynamic Capability Reporting**
- Platform-aware codec detection based on hardware capabilities
- Real-time queries reflecting actual device configuration
- Separate implementations for different hardware platforms
- Automatic detection of available decoding hardware

### 3. **Dolby Audio Integration**
- Dolby MS12 audio processing support
- Audio mode detection and configuration
- Real-time audio mode change notifications
- Support for Dolby Atmos, Dolby Digital Plus, and legacy formats

### 4. **Resolution & Display Information**
- Current video output resolution reporting
- Display capability queries
- Adaptive to device configuration changes

### 5. **JSON-RPC API**
- RESTful-style JSON-RPC interface for easy integration
- Standardized request/response format
- Event-driven notifications for audio mode changes
- Comprehensive error handling

## Use Cases

### Content Delivery Networks (CDN)
**Scenario**: Adaptive bitrate streaming services need to select appropriate content variants

**Benefits**:
- Query device capabilities before selecting stream quality
- Avoid unsupported codec delivery and playback failures
- Optimize bandwidth usage by selecting native formats
- Reduce CDN costs through efficient content delivery

### Smart TV Applications
**Scenario**: Streaming apps need to determine optimal playback configuration

**Benefits**:
- Select best available audio format (Dolby Atmos vs. stereo)
- Choose appropriate video codec for smooth playback
- Display capability-aware content selection
- Enhanced user experience through optimal format matching

### Device Provisioning & Configuration
**Scenario**: Service operators need to understand device capabilities for content catalog customization

**Benefits**:
- Build device capability profiles
- Filter content catalogs based on actual capabilities
- Optimize content transcoding pipelines
- Reduce support calls from incompatible content

### Automated Testing & Validation
**Scenario**: QA teams need to verify device capabilities across firmware versions

**Benefits**:
- Automated capability regression testing
- Cross-platform compatibility verification
- Quick validation of codec support after firmware updates
- Integration with CI/CD pipelines

### Third-Party Application Integration
**Scenario**: Application developers need standardized API for capability detection

**Benefits**:
- Consistent interface across RDK device variants
- No need for platform-specific detection code
- Simplified multi-platform application development
- Reduced development and maintenance costs

## API Capabilities

### Core Methods

#### `audiocodecs`
Returns list of all supported audio codecs
```json
{
  "audiocodecs": [
    "AudioAAC", "AudioAC3", "AudioEAC3", "AudioDolbyMS12",
    "AudioMP3", "AudioVORBIS", "AudioOPUS"
  ]
}
```

#### `videocodecs`
Returns list of all supported video codecs
```json
{
  "videocodecs": [
    "VideoH264", "VideoH265", "VideoVP9", "VideoAV1", "VideoMPEG"
  ]
}
```

#### `resolution`
Provides current output resolution information
```json
{
  "resolution": "1920x1080"
}
```

#### Dolby Audio Features
Query and configure Dolby-specific audio capabilities
- Get/set audio modes
- Query Dolby MS12 processing status
- Receive audio mode change events

### Event Notifications

#### `audiomodechanged`
Notifies clients when Dolby audio mode changes
```json
{
  "mode": "DolbyDigitalPlus",
  "enabled": true
}
```

## Performance & Reliability

### Performance Characteristics
- **Query Response Time**: < 10ms for cached codec lists
- **Initialization Time**: < 500ms typical startup
- **Memory Footprint**: < 2MB resident memory
- **CPU Utilization**: Negligible (< 1% during queries)

### Reliability Features
- **Error Handling**: Comprehensive error reporting via JSON-RPC
- **Fault Tolerance**: Graceful degradation if platform layers unavailable
- **Connection Management**: Automatic reconnection to platform services
- **Logging**: Detailed diagnostic logging for troubleshooting

### Scalability
- Supports multiple concurrent client connections
- Efficient caching of codec information
- Lock-free codec queries for high-throughput scenarios
- Thunder framework process isolation for stability

## Integration Benefits

### For Content Providers
1. **Optimized Content Delivery**: Select appropriate codec/bitrate combinations
2. **Reduced Bandwidth Costs**: Avoid over-provisioning for unsupported features
3. **Improved QoE**: Deliver best possible quality for each device
4. **Simplified Catalog Management**: Single API across device types

### For Device Manufacturers
1. **Standardized Interface**: Consistent API regardless of hardware platform
2. **Flexible Implementation**: Support for multiple backend HALs
3. **Easy Validation**: Clear capability reporting for testing
4. **Future-Proof Design**: Extensible for new codecs and features

### For Application Developers
1. **Simplified Development**: No platform-specific detection code needed
2. **Cross-Platform Compatibility**: Single codebase for all RDK devices
3. **Real-Time Updates**: Event notifications for dynamic changes
4. **Comprehensive Documentation**: Well-defined interfaces and examples

## Deployment Scenarios

### Set-Top Boxes (STB)
- Full codec enumeration via Device Settings HAL
- IARM bus integration for system-wide coordination
- Dolby MS12 audio processing capabilities
- HDMI audio output configuration

### Smart TVs
- GStreamer-based codec discovery
- Direct hardware decoder queries
- Display-integrated audio processing
- Native resolution reporting

### IP Streaming Devices
- Lightweight GStreamer implementation
- Software decoder fallbacks
- Network-optimized codec selection
- Adaptive quality streaming support

### Hybrid Devices
- Multi-platform implementation support
- Runtime platform detection
- Capability-based feature activation
- Seamless platform migration

## Competitive Advantages

1. **Thunder Framework Integration**: Leverages mature, production-proven plugin infrastructure
2. **Platform Flexibility**: Supports both GStreamer and Device Settings backends
3. **Event-Driven Architecture**: Real-time capability change notifications
4. **Standards Compliance**: JSON-RPC standard for broad compatibility
5. **RDK Ecosystem**: Native integration with RDK middleware stack
6. **Open Source**: Community-driven development and transparency
7. **Production Hardened**: Deployed across millions of RDK devices worldwide

## Future Roadmap

### Planned Enhancements
- HDR format detection (Dolby Vision, HDR10+)
- Extended audio format support (DTS:X, MPEG-H)
- Hardware decoder capability details
- Frame rate and refresh rate reporting
- Advanced video features (10-bit, 4:4:4 chroma)
- DRM system capability reporting

### Integration Opportunities
- Voice assistant integration for capability queries
- Cloud-based device profile management
- Analytics integration for capability tracking
- Enhanced diagnostic reporting
