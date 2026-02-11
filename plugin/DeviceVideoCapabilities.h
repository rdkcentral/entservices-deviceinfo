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
    class DeviceVideoCapabilities : public Exchange::IDeviceVideoCapabilities {
    private:
        DeviceVideoCapabilities(const DeviceVideoCapabilities&) = delete;
        DeviceVideoCapabilities& operator=(const DeviceVideoCapabilities&) = delete;

    public:
        DeviceVideoCapabilities();

        BEGIN_INTERFACE_MAP(DeviceVideoCapabilities)
        INTERFACE_ENTRY(Exchange::IDeviceVideoCapabilities)
        END_INTERFACE_MAP

    private:
        // IDeviceVideoCapabilities interface
        Core::hresult SupportedVideoDisplays(RPC::IStringIterator*& supportedVideoDisplays, bool& success) const override;
        Core::hresult HostEDID(HostEdid& hostEdid) const override;
        Core::hresult DefaultResolution(const string& videoDisplay, DefaultResln& defaultResln) const override;
        Core::hresult SupportedResolutions(const string& videoDisplay, RPC::IStringIterator*& supportedResolutions, bool& success) const override;
        Core::hresult SupportedHdcp(const string& videoDisplay, SupportedHDCPVer& supportedHDCPVer) const override;
    };
}
}
