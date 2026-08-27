/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2025 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * PlayerInfoImplementation — DS COM-RPC variant
 *
 * Compiled when USE_DEVICESETTING_PLUGIN=ON.
 * Replaces DeviceSettings/PlatformImplementation.cpp (which links libds + IARM).
 *
 * libds / IARM / device:: headers are NOT included here.
 * DS queries go through the entservices-devicesettings COM-RPC plugin using
 * DSHelper::AcquireSubInterface<T>().
 *
 * Mapping: old libds call → new DS COM-RPC call
 *  Resolution()
 *    device::Host::getDefaultVideoPortName()
 *    vPort.getResolution().getName()
 *    → IDeviceSettingsVideoPort::GetVideoPort(type, index, handle)
 *      + GetVideoPortResolution(handle, vpRes) → vpRes.name
 *
 *  IsAudioEquivalenceEnabled()
 *    device::Host::isHDMIOutPortPresent()   → AudioConfigStore::IsHDMIOutPortPresent()
 *    aPort.GetLEConfig()                    → IDeviceSettingsAudio::GetAudioLEConfig(handle)
 *
 *  AtmosMetadata()
 *    iterate aPorts for "HDMI_ARC"          → GetAudioPort(AUDIO_PORT_TYPE_HDMIARC, 0)
 *    aPort.getSinkDeviceAtmosCapability()   → GetAudioSinkDeviceAtmosCapability(handle, cap)
 *
 *  SoundMode()
 *    iterate all ports by priority          → AudioConfigStore entries
 *    aPort.getStereoMode()                  → GetStereoMode(handle, mode)
 *    aPort.getStereoAuto()                  → GetStereoAuto(handle, auto)
 *
 *  EnableAtmosOutput(enable)
 *    device::Host::isHDMIOutPortPresent()   → AudioConfigStore::IsHDMIOutPortPresent()
 *    aPort.setAudioAtmosOutputMode(enable)  → SetAudioAtmosOutputMode(handle, enable)
 *
 *  OnAudioModeEvent HAL callback
 *    device::Host::IAudioOutputPortEvents   → IDeviceSettingsAudio::INotification::OnAudioModeEvent
 */

#include "../Module.h"
#include <interfaces/IPlayerInfo.h>
#include <interfaces/IDolby.h>
#include <interfaces/IConfiguration.h>
#include <interfaces/IDeviceSettingsAudio.h>
#include <interfaces/IDeviceSettingsVideoPort.h>

#include <gst/gst.h>

#include "DeviceSettingsInterface.h"
#include "UtilsSearchRDKProfile.h"

namespace WPEFramework {
namespace Plugin {

class PlayerInfoImplementation
    : public Exchange::IPlayerProperties
    , public Exchange::Dolby::IOutput
    , public Exchange::IConfiguration
    , public DSHelper
{
private:
    // =========================================================================
    // GStreamer codec detection helper (unchanged from DeviceSettings/ variant)
    // =========================================================================
    class GstUtils {
    private:
        struct FeatureListDeleter {
            void operator()(GList* p) { gst_plugin_feature_list_free(p); }
        };
        struct CapsDeleter {
            void operator()(GstCaps* p) { gst_caps_unref(p); }
        };
        typedef std::unique_ptr<GList, FeatureListDeleter> FeatureList;
        typedef std::unique_ptr<GstCaps, CapsDeleter>      MediaTypes;

    public:
        GstUtils()                         = delete;
        GstUtils(const GstUtils&)          = delete;
        GstUtils& operator=(const GstUtils&) = delete;

        template <typename C, typename CodecIteratorList>
        static bool GstRegistryCheckElementsForMediaTypes(C caps, CodecIteratorList& list)
        {
            auto type = std::is_same<C, VideoCaps>::value
                            ? GST_ELEMENT_FACTORY_TYPE_MEDIA_VIDEO
                            : GST_ELEMENT_FACTORY_TYPE_MEDIA_AUDIO;

            FeatureList decoderFactories { gst_element_factory_list_get_elements(
                GST_ELEMENT_FACTORY_TYPE_DECODER | type, GST_RANK_MARGINAL) };
            FeatureList parserFactories  { gst_element_factory_list_get_elements(
                GST_ELEMENT_FACTORY_TYPE_PARSER  | type, GST_RANK_MARGINAL) };

            FeatureList elements;
            for (const auto& index : caps) {
                MediaTypes mediaType { gst_caps_from_string(index.first.c_str()) };
                if (elements = std::move(GstRegistryGetElementForMediaType(
                        decoderFactories.get(), std::move(mediaType)))) {
                    list.push_back(index.second);
                } else if (elements = std::move(GstRegistryGetElementForMediaType(
                                parserFactories.get(), std::move(mediaType)))) {
                    for (GList* it = elements.get(); it; it = it->next) {
                        GstElementFactory* factory = static_cast<GstElementFactory*>(it->data);
                        const GList* pads = gst_element_factory_get_static_pad_templates(factory);
                        for (const GList* pit = pads; pit; pit = pit->next) {
                            GstStaticPadTemplate* pad = static_cast<GstStaticPadTemplate*>(pit->data);
                            if (pad->direction == GST_PAD_SRC) {
                                MediaTypes srcTypes { gst_static_pad_template_get_caps(pad) };
                                if (GstRegistryGetElementForMediaType(
                                        decoderFactories.get(), std::move(srcTypes))) {
                                    list.push_back(index.second);
                                }
                            }
                        }
                    }
                }
            }
            return (list.size() != 0);
        }

    private:
        static inline FeatureList GstRegistryGetElementForMediaType(
            GList* factories, MediaTypes&& mediaTypes)
        {
            FeatureList candidates { gst_element_factory_list_filter(
                factories, mediaTypes.get(), GST_PAD_SINK, false) };
            return candidates;
        }
    };

    // =========================================================================
    // DS Audio event notification — delegates OnAudioModeEvent to parent
    // =========================================================================
    class DSAudioNotification : public Exchange::IDeviceSettingsAudio::INotification {
    public:
        explicit DSAudioNotification(PlayerInfoImplementation& parent)
            : _parent(parent)
        {
        }

        void OnAudioModeEvent(
            Exchange::IDeviceSettingsAudio::AudioPortType  portType,
            Exchange::IDeviceSettingsAudio::StereoMode     audioMode) override
        {
            _parent.OnDSAudioModeEvent(portType, audioMode);
        }

        BEGIN_INTERFACE_MAP(DSAudioNotification)
            INTERFACE_ENTRY(Exchange::IDeviceSettingsAudio::INotification)
        END_INTERFACE_MAP
    private:
        PlayerInfoImplementation& _parent;
    };

    // =========================================================================
    // Iterator type aliases (same as DeviceSettings/ variant)
    // =========================================================================
    using AudioIteratorImplementation =
        RPC::IteratorType<Exchange::IPlayerProperties::IAudioCodecIterator>;
    using VideoIteratorImplementation =
        RPC::IteratorType<Exchange::IPlayerProperties::IVideoCodecIterator>;

    typedef std::map<const string, const Exchange::IPlayerProperties::AudioCodec> AudioCaps;
    typedef std::map<const string, const Exchange::IPlayerProperties::VideoCodec> VideoCaps;

    // Shorten frequently-used type aliases
    using AudioPortType        = Exchange::IDeviceSettingsAudio::AudioPortType;
    using StereoMode           = Exchange::IDeviceSettingsAudio::StereoMode;
    using DolbyAtmosCapability = Exchange::IDeviceSettingsAudio::DolbyAtmosCapability;
    using VideoPortEntry       = ::WPEFramework::Plugin::VideoPortEntry;
    using AudioPortEntry       = ::WPEFramework::Plugin::AudioPortEntry;

public:
    // =========================================================================
    // Constructor / Destructor
    // =========================================================================
    PlayerInfoImplementation()
        : _dsAudioNotification(*this)
    {
        gst_init(0, nullptr);
        UpdateAudioCodecInfo();
        UpdateVideoCodecInfo();
        PlayerInfoImplementation::_instance = this;
    }

    PlayerInfoImplementation(const PlayerInfoImplementation&)            = delete;
    PlayerInfoImplementation& operator=(const PlayerInfoImplementation&) = delete;

    ~PlayerInfoImplementation() override
    {
        // Unregister audio notification before the DS link is closed
        auto* audio = DSHelper::AcquireSubInterface<Exchange::IDeviceSettingsAudio>();
        if (audio != nullptr) {
            audio->Unregister(&_dsAudioNotification);
            audio->Release();
        }
        DSHelper::Close();
        _audioCodecs.clear();
        _videoCodecs.clear();
        PlayerInfoImplementation::_instance = nullptr;
    }

    // =========================================================================
    // Exchange::IConfiguration — called by Thunder after instantiation
    // =========================================================================
    Core::hresult Configure(PluginHost::IShell* service) override
    {
        // Opens link to DeviceSettings plugin.
        // If DS is already active, OnDeviceSettingsActivated() is called immediately.
        const uint32_t result = DSHelper::Open(service);
        if (result != Core::ERROR_NONE) {
            LOGERR("Configure: Failed to open DeviceSettings link (result=%u)", result);
        }
        return Core::ERROR_NONE;
    }

    // =========================================================================
    // Exchange::IPlayerProperties
    // =========================================================================
    uint32_t AudioCodecs(Exchange::IPlayerProperties::IAudioCodecIterator*& iterator) const override
    {
        iterator = Core::Service<AudioIteratorImplementation>::Create<
            Exchange::IPlayerProperties::IAudioCodecIterator>(_audioCodecs);
        return (iterator != nullptr ? Core::ERROR_NONE : Core::ERROR_GENERAL);
    }

    uint32_t VideoCodecs(Exchange::IPlayerProperties::IVideoCodecIterator*& iterator) const override
    {
        iterator = Core::Service<VideoIteratorImplementation>::Create<
            Exchange::IPlayerProperties::IVideoCodecIterator>(_videoCodecs);
        return (iterator != nullptr ? Core::ERROR_NONE : Core::ERROR_GENERAL);
    }

    uint32_t Resolution(PlaybackResolution& res /* @out */) const override
    {
        res = RESOLUTION_UNKNOWN;

        // Acquire VideoPort sub-interface
        auto* vp = const_cast<PlayerInfoImplementation*>(this)->DSHelper::AcquireSubInterface<Exchange::IDeviceSettingsVideoPort>();
        if (vp == nullptr) {
            LOGERR("Resolution: IDeviceSettingsVideoPort unavailable");
            return Core::ERROR_NONE;
        }

        string currentResolution;

        // Find the default video port (HDMI0 preferred, first entry as fallback)
        // DSHelper accessors are thread-safe — no external lock needed.
        std::string defaultPortName = DSHelper::getDefaultVideoPortName();
        VideoPortEntry entry;
        bool found = DSHelper::resolveVideoPortByName(defaultPortName, entry);

        Exchange::IDeviceSettingsVideoPort::VideoPortResolution vpRes{};
        bool vpResValid = false;
        if (found) {
            // Use cached handle populated by DSHelper::LoadAllConfigs()
            int32_t handle = DSHelper::getCachedVideoPortHandle(defaultPortName);
            if (handle != INVALID_DS_HANDLE) {
                if (vp->GetVideoPortResolution(handle, vpRes) == Core::ERROR_NONE) {
                    currentResolution = vpRes.name;
                    vpResValid = true;
                    LOGINFO("Resolution: name='%s' pixelRes=%d frameRate=%d interlaced=%s",
                            currentResolution.c_str(),
                            static_cast<int>(vpRes.pixelResolution),
                            static_cast<int>(vpRes.frameRate),
                            vpRes.interlaced ? "true" : "false");
                } else {
                    LOGERR("Resolution: GetVideoPortResolution failed for handle=%d", handle);
                }
            } else {
                LOGERR("Resolution: cached handle not found for port '%s'", defaultPortName.c_str());
            }
        } else {
            LOGWARN("Resolution: video config not yet loaded — DS may not be active");
        }

        vp->Release();

        if (!currentResolution.empty()) {
            // 1. Exact named string match ("1080p60", "720p", etc.)
            auto it = _resolutions.find(currentResolution);
            if (it != _resolutions.end()) {
                res = it->second;
            } else {
                // 2. Strip 2-char frame-rate suffix ("1080p60" → "1080p")
                if (currentResolution.size() > 2) {
                    string baseRes = currentResolution.substr(0, currentResolution.size() - 2);
                    auto it2 = _resolutions.find(baseRes);
                    if (it2 != _resolutions.end()) {
                        res = it2->second;
                    }
                }
                // 3. WxH dimension string ("1920x1080", etc.)
                if (res == RESOLUTION_UNKNOWN) {
                    auto it3 = _resolutionsByDimension.find(currentResolution);
                    if (it3 != _resolutionsByDimension.end()) {
                        res = it3->second;
                        LOGINFO("Resolution: matched by dimension string '%s'", currentResolution.c_str());
                    }
                }
            }
        }

        // 4. Final fallback: use VideoPortResolution struct fields directly
        //    (pixelResolution enum + interlaced flag) — bypasses string parsing entirely
        if (res == RESOLUTION_UNKNOWN && vpResValid) {
            using VR = Exchange::IDeviceSettingsVideoPort::VideoResolution;
            res = vpRes.interlaced
                ? (vpRes.pixelResolution == VR::DS_VIDEO_PIXELRES_1920X1080 ? RESOLUTION_1080I
                 : vpRes.pixelResolution == VR::DS_VIDEO_PIXELRES_720X480    ? RESOLUTION_480I
                 : vpRes.pixelResolution == VR::DS_VIDEO_PIXELRES_720X576    ? RESOLUTION_576I
                 : RESOLUTION_UNKNOWN)
                : (vpRes.pixelResolution == VR::DS_VIDEO_PIXELRES_1920X1080  ? RESOLUTION_1080P
                 : vpRes.pixelResolution == VR::DS_VIDEO_PIXELRES_1280X720   ? RESOLUTION_720P
                 : vpRes.pixelResolution == VR::DS_VIDEO_PIXELRES_3840X2160  ? RESOLUTION_2160P
                 : vpRes.pixelResolution == VR::DS_VIDEO_PIXELRES_4096X2160  ? RESOLUTION_2160P
                 : vpRes.pixelResolution == VR::DS_VIDEO_PIXELRES_1366X768   ? RESOLUTION_768P
                 : vpRes.pixelResolution == VR::DS_VIDEO_PIXELRES_720X480    ? RESOLUTION_480P
                 : vpRes.pixelResolution == VR::DS_VIDEO_PIXELRES_720X576    ? RESOLUTION_576P
                 : RESOLUTION_UNKNOWN);
            if (res != RESOLUTION_UNKNOWN) {
                LOGINFO("Resolution: matched by pixelResolution=%d interlaced=%s",
                        static_cast<int>(vpRes.pixelResolution), vpRes.interlaced ? "true" : "false");
            } else {
                LOGWARN("Resolution: all lookups failed for '%s' pixelRes=%d",
                        currentResolution.c_str(), static_cast<int>(vpRes.pixelResolution));
            }
        }

        return Core::ERROR_NONE;
    }

    uint32_t IsAudioEquivalenceEnabled(bool& isEnabled /* @out */) const override
    {
        isEnabled = false;

        // DSHelper accessors are thread-safe — no external lock needed.
        bool hdmiPresent = DSHelper::isHDMIAudioOutPortPresent();

        auto* audio = const_cast<PlayerInfoImplementation*>(this)->DSHelper::AcquireSubInterface<Exchange::IDeviceSettingsAudio>();
        if (audio == nullptr) {
            LOGERR("IsAudioEquivalenceEnabled: IDeviceSettingsAudio unavailable");
            return Core::ERROR_NONE;
        }

        // Use cached handles populated by DSHelper::LoadAllConfigs()
        int32_t handle = hdmiPresent ? DSHelper::getCachedAudioPortHandle("HDMI0") : INVALID_DS_HANDLE;
        if (hdmiPresent) {
            LOGINFO("IsAudioEquivalenceEnabled: hdmiPresent=true, handle=%d", handle);
            if (handle != INVALID_DS_HANDLE) {
                bool enabled = false;
                uint32_t enabledRc = audio->IsAudioPortEnabled(handle, enabled);
                LOGINFO("IsAudioEquivalenceEnabled: IsAudioPortEnabled rc=%u enabled=%s",
                        enabledRc, enabled ? "true" : "false");
                if (enabledRc == Core::ERROR_NONE && enabled) {
                    audio->GetAudioLEConfig(handle, isEnabled);
                    LOGINFO("IsAudioEquivalenceEnabled (HDMI0) = %s",
                            isEnabled ? "Enabled" : "Disabled");
                } else {
                    LOGWARN("IsAudioEquivalenceEnabled: HDMI0 not enabled/connected (rc=%u enabled=%s)",
                            enabledRc, enabled ? "true" : "false");
                }
            }
        } else {
            // Fallback to SPEAKER0 — use cached handle
            LOGINFO("IsAudioEquivalenceEnabled: hdmiPresent=false, falling back to SPEAKER0");
            handle = DSHelper::getCachedAudioPortHandle("SPEAKER0");
            if (handle != INVALID_DS_HANDLE) {
                LOGINFO("IsAudioEquivalenceEnabled: SPEAKER0 handle=%d", handle);
                audio->GetAudioLEConfig(handle, isEnabled);
                LOGINFO("IsAudioEquivalenceEnabled (SPEAKER0) = %s",
                        isEnabled ? "Enabled" : "Disabled");
            }
        }

        audio->Release();
        return Core::ERROR_NONE;
    }

    // =========================================================================
    // Exchange::Dolby::IOutput — notification management
    // =========================================================================
    uint32_t Register(Exchange::Dolby::IOutput::INotification* notification) override
    {
        _adminLock.Lock();
        ASSERT(std::find(_observers.begin(), _observers.end(), notification) == _observers.end());
        _observers.push_back(notification);
        notification->AddRef();
        _adminLock.Unlock();
        return Core::ERROR_NONE;
    }

    uint32_t Unregister(Exchange::Dolby::IOutput::INotification* notification) override
    {
        _adminLock.Lock();
        auto it = std::find(_observers.begin(), _observers.end(), notification);
        ASSERT(it != _observers.end());
        if (it != _observers.end()) {
            (*it)->Release();
            _observers.erase(it);
        }
        _adminLock.Unlock();
        return Core::ERROR_NONE;
    }

    // Mode setter / getter — not implemented by this platform
    uint32_t Mode(const Exchange::Dolby::IOutput::Type& /*mode*/) override
    {
        return Core::ERROR_GENERAL;
    }

    uint32_t Mode(Exchange::Dolby::IOutput::Type& /*mode*/) const override
    {
        return Core::ERROR_GENERAL;
    }

    uint32_t AtmosMetadata(bool& supported /* @out */) const override
    {
        supported = false;

        auto* audio = const_cast<PlayerInfoImplementation*>(this)->DSHelper::AcquireSubInterface<Exchange::IDeviceSettingsAudio>();
        if (audio == nullptr) {
            LOGERR("AtmosMetadata: IDeviceSettingsAudio unavailable");
            return Core::ERROR_NONE;
        }

        // Use cached handles populated by DSHelper::LoadAllConfigs()
        int32_t arcHandle  = DSHelper::getCachedAudioPortHandle("HDMI_ARC0");
        int32_t hdmiHandle = DSHelper::getCachedAudioPortHandle("HDMI0");

        if (TV == searchRdkProfile()) {
            // TV platform: use persisted user-intent enable state — HAL state is unreliable on some platforms
            bool arcEnabled = false;
            if (arcHandle != INVALID_DS_HANDLE) {
                string portName = "HDMI_ARC0";
                audio->GetAudioEnablePersist(arcHandle, arcEnabled, portName);
                LOGINFO("AtmosMetadata: GetAudioEnablePersist(HDMI_ARC0) = %s", arcEnabled ? "true" : "false");
            }

            DolbyAtmosCapability capability = DolbyAtmosCapability::AUDIO_DOLBY_ATMOS_NOT_SUPPORTED;
            if (arcEnabled && arcHandle != INVALID_DS_HANDLE) {
                // ARC is enabled — query HDMI_ARC0 for ATMOS capability
                LOGINFO("AtmosMetadata: ARC enabled, querying HDMI_ARC0 for ATMOS capability");
                audio->GetAudioSinkDeviceAtmosCapability(arcHandle, capability);
            } else {
                // ARC not enabled — query HDMI0 (TV panel itself)
                LOGINFO("AtmosMetadata: ARC not enabled, querying HDMI0 for ATMOS capability");
                int32_t selectedHandle = (hdmiHandle != INVALID_DS_HANDLE) ? hdmiHandle
                                       : DSHelper::getCachedAudioPortHandle("SPEAKER0");
                if (selectedHandle != INVALID_DS_HANDLE) {
                    audio->GetAudioSinkDeviceAtmosCapability(selectedHandle, capability);
                } else {
                    LOGWARN("AtmosMetadata: no HDMI_ARC (enabled), HDMI, or SPEAKER port found");
                }
            }
            supported = (capability == DolbyAtmosCapability::AUDIO_DOLBY_ATMOS_METADATA);
            LOGINFO("AtmosMetadata: capability=%d, supported=%s",
                    static_cast<int>(capability), supported ? "true" : "false");
        } else {
            // STB platform: keep port-priority logic (HDMI_ARC > HDMI > SPEAKER)
            int32_t selectedHandle = INVALID_DS_HANDLE;
            bool    arcConnected   = false;

            if (arcHandle != INVALID_DS_HANDLE) {
                bool arcEnabled = false;
                audio->IsAudioPortEnabled(arcHandle, arcEnabled);
                if (arcEnabled) {
                    selectedHandle = arcHandle;
                    arcConnected   = true;
                }
            }

            if (!arcConnected && hdmiHandle != INVALID_DS_HANDLE) {
                bool hdmiEnabled = false;
                audio->IsAudioPortEnabled(hdmiHandle, hdmiEnabled);
                if (hdmiEnabled) {
                    selectedHandle = hdmiHandle;
                }
            }

            if (selectedHandle != INVALID_DS_HANDLE) {
                DolbyAtmosCapability capability = DolbyAtmosCapability::AUDIO_DOLBY_ATMOS_NOT_SUPPORTED;
                audio->GetAudioSinkDeviceAtmosCapability(selectedHandle, capability);
                supported = (capability == DolbyAtmosCapability::AUDIO_DOLBY_ATMOS_METADATA);
                LOGINFO("AtmosMetadata: capability=%d, supported=%s",
                        static_cast<int>(capability), supported ? "true" : "false");
            } else {
                int32_t spkHandle = DSHelper::getCachedAudioPortHandle("SPEAKER0");
                if (spkHandle != INVALID_DS_HANDLE) {
                    DolbyAtmosCapability capability = DolbyAtmosCapability::AUDIO_DOLBY_ATMOS_NOT_SUPPORTED;
                    audio->GetAudioSinkDeviceAtmosCapability(spkHandle, capability);
                    supported = (capability == DolbyAtmosCapability::AUDIO_DOLBY_ATMOS_METADATA);
                    LOGINFO("AtmosMetadata (SPEAKER fallback): capability=%d, supported=%s",
                            static_cast<int>(capability), supported ? "true" : "false");
                } else {
                    LOGWARN("AtmosMetadata: no connected HDMI_ARC, HDMI, or SPEAKER port found");
                }
            }
        }

        audio->Release();
        return Core::ERROR_NONE;
    }

    uint32_t SoundMode(Exchange::Dolby::IOutput::SoundModes& mode /* @out */) const override
    {
        mode = UNKNOWN;

        // Port priority: HDMI_ARC > HDMI > SPEAKER > SPDIF > HEADPHONE
        static const AudioPortType kPriority[] = {
            AudioPortType::AUDIO_PORT_TYPE_HDMIARC,
            AudioPortType::AUDIO_PORT_TYPE_HDMI,
            AudioPortType::AUDIO_PORT_TYPE_SPEAKER,
            AudioPortType::AUDIO_PORT_TYPE_SPDIF,
            AudioPortType::AUDIO_PORT_TYPE_HEADPHONE
        };
        static const size_t kPriorityCount = sizeof(kPriority) / sizeof(kPriority[0]);

        auto* audio = const_cast<PlayerInfoImplementation*>(this)->DSHelper::AcquireSubInterface<Exchange::IDeviceSettingsAudio>();
        if (audio == nullptr) {
            LOGERR("SoundMode: IDeviceSettingsAudio unavailable");
            return Core::ERROR_NONE;
        }

        // Fetch entries and all cached handles via DSHelper (thread-safe, no external lock needed)
        std::vector<AudioPortEntry> entries;
        DSHelper::getAudioPortEntries(entries);
        std::vector<int32_t> handles(entries.size(), INVALID_DS_HANDLE);
        for (size_t i = 0; i < entries.size(); ++i) {
            handles[i] = DSHelper::getCachedAudioPortHandle(entries[i].name);
        }

        bool found = false;
        for (size_t pi = 0; pi < kPriorityCount && !found; ++pi) {
            AudioPortType targetType = kPriority[pi];

            for (size_t ei = 0; ei < entries.size() && !found; ++ei) {
                if (entries[ei].type != targetType) continue;

                int32_t handle = handles[ei];
                if (handle == INVALID_DS_HANDLE) {
                    continue;
                }

                // Mirrors isEnabled(): skip only when the HAL explicitly reports the port as disabled.
                bool enabled = false;
                if (audio->IsAudioPortEnabled(handle, enabled) == Core::ERROR_NONE && !enabled) continue;

                // Mirrors isConnected(): HDMI→display connected, ARC→HDMI-In connected, others→always true.
                int32_t connHandle = INVALID_DS_HANDLE;
                if (!const_cast<PlayerInfoImplementation*>(this)->DSHelper::isAudioOutputPortConnected(
                        audio, entries[ei].name, connHandle)) continue;

                StereoMode stereoMode = StereoMode::AUDIO_STEREO_UNKNOWN;
                if (audio->GetStereoMode(handle, stereoMode, false) == Core::ERROR_NONE) {
                    mode = DsAudioModeToSoundMode(stereoMode);

                    // Auto mode for HDMI_ARC and SPDIF (pass-through detection)
                    if (targetType == AudioPortType::AUDIO_PORT_TYPE_HDMIARC
                        || targetType == AudioPortType::AUDIO_PORT_TYPE_SPDIF) {
                        int32_t autoMode = 0;
                        if (audio->GetStereoAuto(handle, autoMode) == Core::ERROR_NONE && autoMode) {
                            mode = SOUNDMODE_AUTO;
                        }
                    }

                    LOGINFO("SoundMode: port type=%d index=%d → SoundMode=%d",
                            static_cast<int>(targetType), entries[ei].index, static_cast<int>(mode));
                    found = true;
                }
            }
        }

        if (!found) {
            LOGWARN("SoundMode: no enabled+connected audio port found matching priority");
        }

        audio->Release();
        return Core::ERROR_NONE;
    }

    uint32_t EnableAtmosOutput(const bool& enable /* @in */)
    {
        // DSHelper accessors are thread-safe — no external lock needed.
        bool hdmiPresent = DSHelper::isHDMIAudioOutPortPresent();

        auto* audio = DSHelper::AcquireSubInterface<Exchange::IDeviceSettingsAudio>();
        if (audio == nullptr) {
            LOGERR("EnableAtmosOutput: IDeviceSettingsAudio unavailable");
            return Core::ERROR_NONE;
        }

        // Use cached handle populated by DSHelper::LoadAllConfigs()
        int32_t handle = hdmiPresent ? DSHelper::getCachedAudioPortHandle("HDMI0") : INVALID_DS_HANDLE;
        if (hdmiPresent && handle != INVALID_DS_HANDLE) {
            bool enabled = false;
            if (audio->IsAudioPortEnabled(handle, enabled) == Core::ERROR_NONE && enabled) {
                audio->SetAudioAtmosOutputMode(handle, enable);
                LOGINFO("EnableAtmosOutput: HDMI0 atmos=%s", enable ? "on" : "off");
            } else {
                LOGWARN("EnableAtmosOutput: HDMI0 not enabled/connected");
            }
        } else if (!hdmiPresent) {
            // No HDMI — enumerate ports and use the first available via DSHelper
            auto portHandles = DSHelper::getAudioPortHandleEntries();

            for (const auto& kv : portHandles) {
                if (kv.second != INVALID_DS_HANDLE) {
                    audio->SetAudioAtmosOutputMode(kv.second, enable);
                    LOGINFO("EnableAtmosOutput: fallback port '%s' atmos=%s",
                            kv.first.c_str(), enable ? "on" : "off");
                    break;
                }
            }
        }

        audio->Release();
        return Core::ERROR_NONE;
    }

    BEGIN_INTERFACE_MAP(PlayerInfoImplementation)
        INTERFACE_ENTRY(Exchange::IPlayerProperties)
        INTERFACE_ENTRY(Exchange::Dolby::IOutput)
        INTERFACE_ENTRY(Exchange::IConfiguration)
    END_INTERFACE_MAP

protected:
    // =========================================================================
    // DSHelper overrides — DS lifecycle
    // =========================================================================

    /**
     * Called when DeviceSettings plugin (re-)activates.
     * Config (VideoPort, Audio) is already loaded by DSHelper::LoadAllConfigs()
     * before this override is called. Only notification registration is needed.
     */
    void OnDeviceSettingsActivated() override
    {
        LOGINFO("PlayerInfo: OnDeviceSettingsActivated — registering audio notification");

        // Register for audio mode change events.
        // Config is already loaded by DSHelper::LoadAllConfigs() (via GetDeviceSettingConfigs).
        auto* audio = DSHelper::AcquireSubInterface<Exchange::IDeviceSettingsAudio>();
        if (audio != nullptr) {
            audio->Register("PlayerInfo", &_dsAudioNotification);
            audio->Release();
        } else {
            LOGWARN("OnDeviceSettingsActivated: IDeviceSettingsAudio not available for Register");
        }
    }

    /**
     * Called when DeviceSettings plugin deactivates.
     * DSHelper manages config lifecycle internally — no manual Clear() needed.
     * Do NOT call AcquireSubInterface<>() here — the link is already down.
     */
    void OnDeviceSettingsDeactivated() override
    {
        LOGINFO("PlayerInfo: OnDeviceSettingsDeactivated");
    }

private:
    // =========================================================================
    // DS Audio mode event handler
    // =========================================================================
    void OnDSAudioModeEvent(AudioPortType /*portType*/, StereoMode audioMode)
    {
        LOGINFO("PlayerInfo: OnDSAudioModeEvent received");
        Exchange::Dolby::IOutput::SoundModes soundMode = DsAudioModeToSoundMode(audioMode);
        AudiomodeChanged(soundMode, true);
    }

    void AudiomodeChanged(Exchange::Dolby::IOutput::SoundModes mode, bool enable)
    {
        _adminLock.Lock();
        for (auto* obs : _observers) {
            obs->AudioModeChanged(mode, enable);
        }
        _adminLock.Unlock();
    }

    // =========================================================================
    // Enum conversion: DS StereoMode → Dolby SoundModes
    // =========================================================================
    static Exchange::Dolby::IOutput::SoundModes DsAudioModeToSoundMode(StereoMode mode)
    {
        switch (mode) {
        case StereoMode::AUDIO_STEREO_MONO:        return MONO;
        case StereoMode::AUDIO_STEREO_STEREO:      return STEREO;
        case StereoMode::AUDIO_STEREO_SURROUND:    return SURROUND;
        case StereoMode::AUDIO_STEREO_PASSTHROUGH: return PASSTHRU;
        case StereoMode::AUDIO_STEREO_DD:          return DOLBYDIGITAL;
        case StereoMode::AUDIO_STEREO_DDPLUS:      return DOLBYDIGITALPLUS;
        default:
            LOGWARN("DsAudioModeToSoundMode: unknown StereoMode=%d", static_cast<int>(mode));
            return UNKNOWN;
        }
    }

    // =========================================================================
    // GStreamer codec discovery (identical to DeviceSettings/ variant)
    // =========================================================================
    void UpdateAudioCodecInfo()
    {
        AudioCaps audioCaps = {
            { "audio/mpeg, mpegversion=(int)1",                          Exchange::IPlayerProperties::AUDIO_MPEG1       },
            { "audio/mpeg, mpegversion=(int)2",                          Exchange::IPlayerProperties::AUDIO_MPEG2       },
            { "audio/mpeg, mpegversion=(int)4",                          Exchange::IPlayerProperties::AUDIO_MPEG4       },
            { "audio/mpeg, mpegversion=(int)1, layer=(int)[1, 3]",       Exchange::IPlayerProperties::AUDIO_MPEG3       },
            { "audio/mpeg, mpegversion=(int){2, 4}",                     Exchange::IPlayerProperties::AUDIO_AAC         },
            { "audio/x-ac3",                                             Exchange::IPlayerProperties::AUDIO_AC3         },
            { "audio/x-eac3",                                            Exchange::IPlayerProperties::AUDIO_AC3_PLUS    },
            { "audio/x-opus",                                            Exchange::IPlayerProperties::AUDIO_OPUS        },
            { "audio/x-dts",                                             Exchange::IPlayerProperties::AUDIO_DTS         },
            { "audio/x-vorbis",                                          Exchange::IPlayerProperties::AUDIO_VORBIS_OGG  },
            { "audio/x-wav",                                             Exchange::IPlayerProperties::AUDIO_WAV         },
        };
        if (!GstUtils::GstRegistryCheckElementsForMediaTypes(std::move(audioCaps), _audioCodecs)) {
            LOGWARN("UpdateAudioCodecInfo: no Audio Codec support available");
        }
    }

    void UpdateVideoCodecInfo()
    {
        VideoCaps videoCaps = {
            { "video/x-h263",                                                    Exchange::IPlayerProperties::VIDEO_H263  },
            { "video/x-h264, profile=(string)high",                              Exchange::IPlayerProperties::VIDEO_H264  },
            { "video/x-h265",                                                    Exchange::IPlayerProperties::VIDEO_H265  },
            { "video/mpeg, mpegversion=(int){1,2}, systemstream=(boolean)false", Exchange::IPlayerProperties::VIDEO_MPEG  },
            { "video/mpeg, mpegversion=(int)2, systemstream=(boolean)false",     Exchange::IPlayerProperties::VIDEO_MPEG2 },
            { "video/mpeg, mpegversion=(int)4, systemstream=(boolean)false",     Exchange::IPlayerProperties::VIDEO_MPEG4 },
            { "video/x-vp8",                                                     Exchange::IPlayerProperties::VIDEO_VP8   },
            { "video/x-vp9",                                                     Exchange::IPlayerProperties::VIDEO_VP9   },
            { "video/x-vp10",                                                    Exchange::IPlayerProperties::VIDEO_VP10  },
            { "video/x-av1",                                                     Exchange::IPlayerProperties::VIDEO_AV1   },
        };
        if (!GstUtils::GstRegistryCheckElementsForMediaTypes(std::move(videoCaps), _videoCodecs)) {
            LOGWARN("UpdateVideoCodecInfo: no Video Codec support available");
        }
    }

    // =========================================================================
    // Member data
    // =========================================================================
    std::list<Exchange::IPlayerProperties::AudioCodec>  _audioCodecs;
    std::list<Exchange::IPlayerProperties::VideoCodec>  _videoCodecs;

    // Resolution string → PlaybackResolution enum (same table as DeviceSettings/ variant)
    std::map<string, Exchange::IPlayerProperties::PlaybackResolution> _resolutions =
    {
        { "480i24",   RESOLUTION_480I24  }, { "480i25",  RESOLUTION_480I25  },
        { "480i30",   RESOLUTION_480I30  }, { "480i50",  RESOLUTION_480I50  },
        { "480i",     RESOLUTION_480I    }, { "480p24",  RESOLUTION_480P24  },
        { "480p25",   RESOLUTION_480P25  }, { "480p30",  RESOLUTION_480P30  },
        { "480p50",   RESOLUTION_480P50  }, { "480p",    RESOLUTION_480P    },
        { "576i24",   RESOLUTION_576I24  }, { "576i25",  RESOLUTION_576I25  },
        { "576i30",   RESOLUTION_576I30  }, { "576i50",  RESOLUTION_576I50  },
        { "576i",     RESOLUTION_576I    }, { "576p24",  RESOLUTION_576P24  },
        { "576p25",   RESOLUTION_576P25  }, { "576p30",  RESOLUTION_576P30  },
        { "576p50",   RESOLUTION_576P50  }, { "576p",    RESOLUTION_576P    },
        { "720p24",   RESOLUTION_720P24  }, { "720p25",  RESOLUTION_720P25  },
        { "720p30",   RESOLUTION_720P30  }, { "720p50",  RESOLUTION_720P50  },
        { "720p",     RESOLUTION_720P    }, { "768p",    RESOLUTION_768P    },
        { "1080i24",  RESOLUTION_1080I24 }, { "1080i25", RESOLUTION_1080I25 },
        { "1080i30",  RESOLUTION_1080I30 }, { "1080i50", RESOLUTION_1080I50 },
        { "1080i",    RESOLUTION_1080I   }, { "1080p24", RESOLUTION_1080P24 },
        { "1080p25",  RESOLUTION_1080P25 }, { "1080p30", RESOLUTION_1080P30 },
        { "1080p50",  RESOLUTION_1080P50 }, { "1080p60", RESOLUTION_1080P  },
        { "1080p",    RESOLUTION_1080P   }, { "2160p24", RESOLUTION_2160P24 },
        { "2160p25",  RESOLUTION_2160P25 }, { "2160p50", RESOLUTION_2160P50 },
        { "2160p30",  RESOLUTION_2160P30 }, { "2160p60", RESOLUTION_2160P60 },
        { "2160p",    RESOLUTION_2160P   }
    };

    // WxH format aliases — HAL may return pixel dimensions instead of named strings
    std::map<string, Exchange::IPlayerProperties::PlaybackResolution> _resolutionsByDimension =
    {
        { "640x480",   RESOLUTION_480P    },
        { "720x480",   RESOLUTION_480P    },
        { "720x576",   RESOLUTION_576P    },
        { "1280x720",  RESOLUTION_720P    },
        { "1366x768",  RESOLUTION_768P    },
        { "1920x1080", RESOLUTION_1080P   },
        { "3840x2160", RESOLUTION_2160P   },
        { "4096x2160", RESOLUTION_2160P   },
    };

    // DS config is managed by DSHelper; access via DSHelper::getDefaultVideoPortName(),
    // DSHelper::getCachedVideoPortHandle(), DSHelper::getCachedAudioPortHandle(), etc.

    // Dolby audio mode change observer list
    std::list<Exchange::Dolby::IOutput::INotification*> _observers;

    mutable Core::CriticalSection _adminLock;

    // DS audio event notification sink — MUST be last (init order)
    Core::Sink<DSAudioNotification> _dsAudioNotification;

public:
    static PlayerInfoImplementation* _instance;
};

PlayerInfoImplementation* PlayerInfoImplementation::_instance = nullptr;
SERVICE_REGISTRATION(PlayerInfoImplementation, 1, 0);

} // namespace Plugin
} // namespace WPEFramework
