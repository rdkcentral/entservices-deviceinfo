/**
* If not stated otherwise in this file or this component's LICENSE
* file the following copyright and licenses apply:
*
* Copyright 2024 RDK Management
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
**/

#include "DeviceAudioCapabilities.h"

#ifdef USE_DEVICESETTING_PLUGIN
#include "DeviceSettingsInterface.h"
#else
#include "exception.hpp"
#include "host.hpp"
#include "manager.hpp"

#include "UtilsIarm.h"
#endif

namespace WPEFramework {
namespace Plugin {

#ifdef USE_DEVICESETTING_PLUGIN
    void DeviceAudioCapabilities::OnDeviceSettingsActivated()
    {
        LOGINFO("DeviceSettingsActivated: loading audio port config");
        auto* audio = AcquireSubInterface<Exchange::IDeviceSettingsAudio>();
        if (audio) {
            LoadAudioConfig(audio, _audioConfig);
            audio->Release();
        } else {
            LOGERR("OnDeviceSettingsActivated: IDeviceSettingsAudio not available");
        }
    }

    void DeviceAudioCapabilities::OnDeviceSettingsDeactivated()
    {
        LOGINFO("DeviceSettingsDeactivated: clearing audio port config");
        _audioConfig.Clear();
    }
#endif

    SERVICE_REGISTRATION(DeviceAudioCapabilities, 1, 0);

    DeviceAudioCapabilities::DeviceAudioCapabilities()
    {
#ifndef USE_DEVICESETTING_PLUGIN
        Utils::IARM::init();

        try {
            device::Manager::Initialize();
        } catch (const device::Exception& e) {
            TRACE(Trace::Fatal, (_T("Exception caught %s"), e.what()));
        } catch (const std::exception& e) {
            TRACE(Trace::Fatal, (_T("Exception caught %s"), e.what()));
        } catch (...) {
        }
#endif
    }

    DeviceAudioCapabilities::~DeviceAudioCapabilities()
    {
#ifdef USE_DEVICESETTING_PLUGIN
        DeviceSettingsClientHelper::Close();
#endif
    }

#ifdef USE_DEVICESETTING_PLUGIN
    uint32_t DeviceAudioCapabilities::Configure(PluginHost::IShell* service)
    {
        DeviceSettingsClientHelper::Open(service);
        return Core::ERROR_NONE;
    }
#endif

    Core::hresult DeviceAudioCapabilities::AudioCapabilities(const string& audioPort, Exchange::IDeviceAudioCapabilities::IAudioCapabilityIterator*& audioCapabilities, bool& success) const
    {
        uint32_t result = Core::ERROR_NONE;

        std::list<Exchange::IDeviceAudioCapabilities::AudioCapability> list;

#ifdef USE_DEVICESETTING_PLUGIN
        // Resolve port from cached config — no per-call GetAudioConfig() round-trip
        if (_audioConfig.IsEmpty()) {
            LOGERR("AudioCapabilities: DeviceSettings config not available");
            return Core::ERROR_UNAVAILABLE;
        }
        std::vector<AudioPortEntry> entries;
        _audioConfig.BuildAudioPortEntries(entries);
        const AudioPortEntry* portEntry = nullptr;
        for (size_t i = 0; i < entries.size() && portEntry == nullptr; ++i) {
            if (audioPort.empty() || entries[i].name == audioPort) {
                portEntry = &entries[i];
            }
        }
        if (portEntry == nullptr) {
            // Port not found: report no capabilities rather than an error
            list.emplace_back(Exchange::IDeviceAudioCapabilities::AudioCapability::AUDIOCAPABILITY_NONE);
        } else {
            auto* audio = AcquireSubInterfaceMutable<Exchange::IDeviceSettingsAudio>();
            if (!audio) {
                LOGERR("AudioCapabilities: IDeviceSettingsAudio interface not available");
                return Core::ERROR_UNAVAILABLE;
            }
            int32_t handle = -1;
            result = audio->GetAudioPort(portEntry->type, portEntry->index, handle);
            if (result == Core::ERROR_NONE) {
                int32_t caps = 0;
                result = audio->GetAudioCapabilities(handle, caps);
                if (result == Core::ERROR_NONE) {
                    if (!caps)
                        list.emplace_back(Exchange::IDeviceAudioCapabilities::AudioCapability::AUDIOCAPABILITY_NONE);
                    if (caps & Exchange::IDeviceSettingsAudio::AUDIO_CAPS_ATMOS)
                        list.emplace_back(Exchange::IDeviceAudioCapabilities::AudioCapability::ATMOS);
                    if (caps & Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DOLBY_DIGITAL)
                        list.emplace_back(Exchange::IDeviceAudioCapabilities::AudioCapability::DD);
                    if (caps & Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DOLBY_DIGITAL_PLUS)
                        list.emplace_back(Exchange::IDeviceAudioCapabilities::AudioCapability::DDPLUS);
                    if (caps & Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DIGITAL_AUDIO_DELIVERY)
                        list.emplace_back(Exchange::IDeviceAudioCapabilities::AudioCapability::DAD);
                    if (caps & Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DIGITAL_AUDIO_PROCESS_V2)
                        list.emplace_back(Exchange::IDeviceAudioCapabilities::AudioCapability::DAPV2);
                    if (caps & Exchange::IDeviceSettingsAudio::AUDIO_CAPS_MS12)
                        list.emplace_back(Exchange::IDeviceAudioCapabilities::AudioCapability::MS12);
                }
            } else {
                list.emplace_back(Exchange::IDeviceAudioCapabilities::AudioCapability::AUDIOCAPABILITY_NONE);
                result = Core::ERROR_NONE;
            }
            audio->Release();
        }
#else
        int capabilities = dsAUDIOSUPPORT_NONE;

        try {
            auto strAudioPort = audioPort.empty() ? device::Host::getInstance().getDefaultAudioPortName() : audioPort;
            auto& aPort = device::Host::getInstance().getAudioOutputPort(strAudioPort);
            aPort.getAudioCapabilities(&capabilities);
        } catch (const device::Exception& e) {
            TRACE(Trace::Fatal, (_T("Exception caught %s"), e.what()));
            result = Core::ERROR_GENERAL;
        } catch (const std::exception& e) {
            TRACE(Trace::Fatal, (_T("Exception caught %s"), e.what()));
            result = Core::ERROR_GENERAL;
        } catch (...) {
            result = Core::ERROR_GENERAL;
        }

        if (!capabilities)
            list.emplace_back(Exchange::IDeviceAudioCapabilities::AudioCapability::AUDIOCAPABILITY_NONE);
        if (capabilities & dsAUDIOSUPPORT_ATMOS)
            list.emplace_back(Exchange::IDeviceAudioCapabilities::AudioCapability::ATMOS);
        if (capabilities & dsAUDIOSUPPORT_DD)
            list.emplace_back(Exchange::IDeviceAudioCapabilities::AudioCapability::DD);
        if (capabilities & dsAUDIOSUPPORT_DDPLUS)
            list.emplace_back(Exchange::IDeviceAudioCapabilities::AudioCapability::DDPLUS);
        if (capabilities & dsAUDIOSUPPORT_DAD)
            list.emplace_back(Exchange::IDeviceAudioCapabilities::AudioCapability::DAD);
        if (capabilities & dsAUDIOSUPPORT_DAPv2)
            list.emplace_back(Exchange::IDeviceAudioCapabilities::AudioCapability::DAPV2);
        if (capabilities & dsAUDIOSUPPORT_MS12)
            list.emplace_back(Exchange::IDeviceAudioCapabilities::AudioCapability::MS12);
#endif

        if (result == Core::ERROR_NONE) {
            audioCapabilities = (Core::Service<RPC::IteratorType<Exchange::IDeviceAudioCapabilities::IAudioCapabilityIterator>>::Create<Exchange::IDeviceAudioCapabilities::IAudioCapabilityIterator>(list));
            success = true;
        }

        return result;
    }

    Core::hresult DeviceAudioCapabilities::MS12Capabilities(const string& audioPort, Exchange::IDeviceAudioCapabilities::IMS12CapabilityIterator*& ms12Capabilities, bool& success) const
    {
        uint32_t result = Core::ERROR_NONE;

        std::list<Exchange::IDeviceAudioCapabilities::MS12Capability> list;

#ifdef USE_DEVICESETTING_PLUGIN
        // Resolve port from cached config — no per-call GetAudioConfig() round-trip
        if (_audioConfig.IsEmpty()) {
            LOGERR("MS12Capabilities: DeviceSettings config not available");
            return Core::ERROR_UNAVAILABLE;
        }
        std::vector<AudioPortEntry> entries;
        _audioConfig.BuildAudioPortEntries(entries);
        const AudioPortEntry* portEntry = nullptr;
        for (size_t i = 0; i < entries.size() && portEntry == nullptr; ++i) {
            if (audioPort.empty() || entries[i].name == audioPort) {
                portEntry = &entries[i];
            }
        }
        if (portEntry == nullptr) {
            list.emplace_back(Exchange::IDeviceAudioCapabilities::MS12Capability::MS12CAPABILITY_NONE);
        } else {
            auto* audio = AcquireSubInterfaceMutable<Exchange::IDeviceSettingsAudio>();
            if (!audio) {
                LOGERR("MS12Capabilities: IDeviceSettingsAudio interface not available");
                return Core::ERROR_UNAVAILABLE;
            }
            int32_t handle = -1;
            result = audio->GetAudioPort(portEntry->type, portEntry->index, handle);
            if (result == Core::ERROR_NONE) {
                int32_t caps = 0;
                result = audio->GetAudioMS12Capabilities(handle, caps);
                if (result == Core::ERROR_NONE) {
                    if (!caps)
                        list.emplace_back(Exchange::IDeviceAudioCapabilities::MS12Capability::MS12CAPABILITY_NONE);
                    if (caps & Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_DOLBYVOLUME)
                        list.emplace_back(Exchange::IDeviceAudioCapabilities::MS12Capability::DOLBYVOLUME);
                    if (caps & Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_INTELLIGENT_EQUALIZER)
                        list.emplace_back(Exchange::IDeviceAudioCapabilities::MS12Capability::INTELIGENTEQUALIZER);
                    if (caps & Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_DIALOG_ENHANCER)
                        list.emplace_back(Exchange::IDeviceAudioCapabilities::MS12Capability::DIALOGUEENHANCER);
                }
            } else {
                list.emplace_back(Exchange::IDeviceAudioCapabilities::MS12Capability::MS12CAPABILITY_NONE);
                result = Core::ERROR_NONE;
            }
            audio->Release();
        }
#else
        int capabilities = dsMS12SUPPORT_NONE;

        try {
            auto strAudioPort = audioPort.empty() ? device::Host::getInstance().getDefaultAudioPortName() : audioPort;
            auto& aPort = device::Host::getInstance().getAudioOutputPort(strAudioPort);
            aPort.getMS12Capabilities(&capabilities);
        } catch (const device::Exception& e) {
            TRACE(Trace::Fatal, (_T("Exception caught %s"), e.what()));
            result = Core::ERROR_GENERAL;
        } catch (const std::exception& e) {
            TRACE(Trace::Fatal, (_T("Exception caught %s"), e.what()));
            result = Core::ERROR_GENERAL;
        } catch (...) {
            result = Core::ERROR_GENERAL;
        }

        if (!capabilities)
            list.emplace_back(Exchange::IDeviceAudioCapabilities::MS12Capability::MS12CAPABILITY_NONE);
        if (capabilities & dsMS12SUPPORT_DolbyVolume)
            list.emplace_back(Exchange::IDeviceAudioCapabilities::MS12Capability::DOLBYVOLUME);
        if (capabilities & dsMS12SUPPORT_InteligentEqualizer)
            list.emplace_back(Exchange::IDeviceAudioCapabilities::MS12Capability::INTELIGENTEQUALIZER);
        if (capabilities & dsMS12SUPPORT_DialogueEnhancer)
            list.emplace_back(Exchange::IDeviceAudioCapabilities::MS12Capability::DIALOGUEENHANCER);
#endif

        if (result == Core::ERROR_NONE) {
            ms12Capabilities = (Core::Service<RPC::IteratorType<Exchange::IDeviceAudioCapabilities::IMS12CapabilityIterator>>::Create<Exchange::IDeviceAudioCapabilities::IMS12CapabilityIterator>(list));
            success = true;
        }

        return result;
    }

    Core::hresult DeviceAudioCapabilities::SupportedMS12AudioProfiles(const string& audioPort, RPC::IStringIterator*& supportedMS12AudioProfiles, bool& success) const
    {
        uint32_t result = Core::ERROR_NONE;

        std::list<string> list;

#ifdef USE_DEVICESETTING_PLUGIN
        // Resolve port from cached config — no per-call GetAudioConfig() round-trip
        if (_audioConfig.IsEmpty()) {
            LOGERR("SupportedMS12AudioProfiles: DeviceSettings config not available");
            return Core::ERROR_UNAVAILABLE;
        }
        std::vector<AudioPortEntry> entries;
        _audioConfig.BuildAudioPortEntries(entries);
        const AudioPortEntry* portEntry = nullptr;
        for (size_t i = 0; i < entries.size() && portEntry == nullptr; ++i) {
            if (audioPort.empty() || entries[i].name == audioPort) {
                portEntry = &entries[i];
            }
        }
        if (portEntry == nullptr) {
            result = Core::ERROR_NONE; // Port not found — return empty list without error
        } else {
            auto* audio = AcquireSubInterfaceMutable<Exchange::IDeviceSettingsAudio>();
            if (!audio) {
                LOGERR("SupportedMS12AudioProfiles: IDeviceSettingsAudio interface not available");
                return Core::ERROR_UNAVAILABLE;
            }
            int32_t handle = -1;
            result = audio->GetAudioPort(portEntry->type, portEntry->index, handle);
            if (result == Core::ERROR_NONE) {
                Exchange::IDeviceSettingsAudio::IDeviceSettingsAudioMS12AudioProfileIterator* profileIter = nullptr;
                result = audio->GetAudioMS12ProfileList(handle, profileIter);
                if (result == Core::ERROR_NONE && profileIter != nullptr) {
                    Exchange::IDeviceSettingsAudio::MS12AudioProfile profile;
                    while (profileIter->Next(profile)) {
                        list.emplace_back(profile.audioProfile);
                    }
                    profileIter->Release();
                }
            } else {
                result = Core::ERROR_NONE; // Port handle not resolved — return empty list
            }
            audio->Release();
        }
#else
        try {
            auto strAudioPort = audioPort.empty() ? device::Host::getInstance().getDefaultAudioPortName() : audioPort;
            auto& aPort = device::Host::getInstance().getAudioOutputPort(strAudioPort);
            const auto supportedProfiles = aPort.getMS12AudioProfileList();
            for (size_t i = 0; i < supportedProfiles.size(); i++) {
                list.emplace_back(supportedProfiles.at(i));
            }
        } catch (const device::Exception& e) {
            TRACE(Trace::Fatal, (_T("Exception caught %s"), e.what()));
            result = Core::ERROR_GENERAL;
        } catch (const std::exception& e) {
            TRACE(Trace::Fatal, (_T("Exception caught %s"), e.what()));
            result = Core::ERROR_GENERAL;
        } catch (...) {
            result = Core::ERROR_GENERAL;
        }
#endif

        if (result == Core::ERROR_NONE) {
            supportedMS12AudioProfiles = (Core::Service<RPC::StringIterator>::Create<RPC::IStringIterator>(list));
            success = true;
        }

        return result;
    }
}
}
