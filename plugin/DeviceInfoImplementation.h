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

#include <interfaces/Ids.h>
#include <interfaces/IDeviceInfo.h>
#include <interfaces/IConfiguration.h>

#include <com/com.h>
#include <core/core.h>

namespace WPEFramework {
namespace Plugin {
    class DeviceInfoImplementation : public Exchange::IDeviceInfo, public Exchange::IConfiguration {
    public:
        // We do not allow this plugin to be copied !!
        DeviceInfoImplementation();
        ~DeviceInfoImplementation() override;

        DeviceInfoImplementation(const DeviceInfoImplementation&) = delete;
        DeviceInfoImplementation& operator=(const DeviceInfoImplementation&) = delete;

        BEGIN_INTERFACE_MAP(DeviceInfoImplementation)
        INTERFACE_ENTRY(Exchange::IDeviceInfo)
        INTERFACE_ENTRY(Exchange::IConfiguration)
        END_INTERFACE_MAP

    public:
        // IDeviceInfo interface
        Core::hresult SerialNumber(DeviceSerialNo& deviceSerialNo) const override;
        Core::hresult Sku(DeviceModelNo& deviceModelNo) const override;
        Core::hresult Make(DeviceMake& deviceMake) const override;
        Core::hresult Model(DeviceModel& deviceModel) const override;
        Core::hresult DeviceType(DeviceTypeInfos& deviceTypeInfos) const override;
        Core::hresult SocName(DeviceSoc& deviceSoc) const override;
        Core::hresult DistributorId(DeviceDistId& deviceDistId) const override;
        Core::hresult Brand(DeviceBrand& deviceBrand) const override;
        Core::hresult ReleaseVersion(DeviceReleaseVer& deviceReleaseVer) const override;
        Core::hresult ChipSet(DeviceChip& deviceChip) const override;
        Core::hresult FirmwareVersion(FirmwareversionInfo& firmwareVersionInfo) const override;
        Core::hresult SystemInfo(SystemInfos& systemInfo) const override;
        Core::hresult Addresses(IAddressesInfoIterator*& addressesInfo) const override;
        Core::hresult EthMac(EthernetMac& ethernetMac) const override;
        Core::hresult EstbMac(StbMac& stbMac) const override;
        Core::hresult WifiMac(WiFiMac& wiFiMa) const override;
        Core::hresult EstbIp(StbIp& stbIp) const override;
        Core::hresult SupportedAudioPorts(RPC::IStringIterator*& supportedAudioPorts, bool& success) const override;

        // IConfiguration interface
        uint32_t Configure(PluginHost::IShell* service) override;

    private:
        PluginHost::IShell* _service;
    };
}
}
