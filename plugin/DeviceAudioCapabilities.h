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

#pragma once

#include "Module.h"
#include <interfaces/IDeviceInfo.h>

#ifdef USE_DEVICESETTING_PLUGIN
#include <interfaces/IConfiguration.h>
#include "DeviceSettingsInterface.h"
#include "DeviceSettingsConfig.h"
#endif

namespace WPEFramework {
namespace Plugin {
    class DeviceAudioCapabilities
        : public Exchange::IDeviceAudioCapabilities
#ifdef USE_DEVICESETTING_PLUGIN
        , public Exchange::IConfiguration
        , public DeviceSettingsClientHelper
#endif
    {
    private:
        DeviceAudioCapabilities(const DeviceAudioCapabilities&) = delete;
        DeviceAudioCapabilities& operator=(const DeviceAudioCapabilities&) = delete;

    public:
        DeviceAudioCapabilities();
        ~DeviceAudioCapabilities() override;

        BEGIN_INTERFACE_MAP(DeviceAudioCapabilities)
        INTERFACE_ENTRY(Exchange::IDeviceAudioCapabilities)
#ifdef USE_DEVICESETTING_PLUGIN
        INTERFACE_ENTRY(Exchange::IConfiguration)
#endif
        END_INTERFACE_MAP

#ifdef USE_DEVICESETTING_PLUGIN
        // IConfiguration: called by DeviceInfo proxy after Root<>() to pass IShell.
        uint32_t Configure(PluginHost::IShell* service) override;
#endif

    private:
        // IDeviceAudioCapabilities interface
        Core::hresult AudioCapabilities(const string& audioPort, Exchange::IDeviceAudioCapabilities::IAudioCapabilityIterator*& audioCapabilities, bool& success) const override;
        Core::hresult MS12Capabilities(const string& audioPort, Exchange::IDeviceAudioCapabilities::IMS12CapabilityIterator*& ms12Capabilities, bool& success) const override;
        Core::hresult SupportedMS12AudioProfiles(const string& audioPort, RPC::IStringIterator*& supportedMS12AudioProfiles, bool& success) const override;

#ifdef USE_DEVICESETTING_PLUGIN
    private:
        template<typename T>
        T* AcquireSubInterfaceMutable() const {
            return const_cast<DeviceAudioCapabilities*>(this)->AcquireSubInterface<T>();
        }

        // Audio config is loaded once in OnDeviceSettingsActivated and cleared on deactivation.
        // All const methods read from the cache — no per-call GetAudioConfig() round-trip.
        mutable AudioConfigStore _audioConfig;

    protected:
        void OnDeviceSettingsActivated() override;
        void OnDeviceSettingsDeactivated() override;
#endif
    };
}
}
