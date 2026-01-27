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

namespace WPEFramework {
namespace Plugin {
    class DeviceAudioCapabilities : public Exchange::IDeviceAudioCapabilities {
    private:
        DeviceAudioCapabilities(const DeviceAudioCapabilities&) = delete;
        DeviceAudioCapabilities& operator=(const DeviceAudioCapabilities&) = delete;

    public:
        DeviceAudioCapabilities();

        BEGIN_INTERFACE_MAP(DeviceAudioCapabilities)
        INTERFACE_ENTRY(Exchange::IDeviceAudioCapabilities)
        END_INTERFACE_MAP

    private:
        // IDeviceAudioCapabilities interface
        Core::hresult AudioCapabilities(const string& audioPort, Exchange::IDeviceAudioCapabilities::IAudioCapabilityIterator*& audioCapabilities, bool& success) const override;
        Core::hresult MS12Capabilities(const string& audioPort, Exchange::IDeviceAudioCapabilities::IMS12CapabilityIterator*& ms12Capabilities, bool& success) const override;
        Core::hresult SupportedMS12AudioProfiles(const string& audioPort, RPC::IStringIterator*& supportedMS12AudioProfiles, bool& success) const override;
    };
}
}
