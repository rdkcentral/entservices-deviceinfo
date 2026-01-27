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

#pragma once

#include "Module.h"
#include <interfaces/IDeviceInfo.h>
#include <interfaces/json/JDeviceInfo.h>
#include <interfaces/json/JsonData_DeviceInfo.h>
#include <interfaces/json/JDeviceAudioCapabilities.h>
#include <interfaces/json/JsonData_DeviceAudioCapabilities.h>
#include <interfaces/json/JDeviceVideoCapabilities.h>
#include <interfaces/json/JsonData_DeviceVideoCapabilities.h>
#include <interfaces/IConfiguration.h>
#include "UtilsLogging.h"
#include "tracing/Logging.h"

namespace WPEFramework 
{
    namespace Plugin
    {
        class DeviceInfo : public PluginHost::IPlugin, public PluginHost::JSONRPC 
        {
            public:
                DeviceInfo(const DeviceInfo&) = delete;
                DeviceInfo& operator=(const DeviceInfo&) = delete;

                DeviceInfo();
                virtual ~DeviceInfo();

                BEGIN_INTERFACE_MAP(DeviceInfo)
                INTERFACE_ENTRY(PluginHost::IPlugin)
                INTERFACE_ENTRY(PluginHost::IDispatcher)
                INTERFACE_AGGREGATE(Exchange::IDeviceInfo, _deviceInfo)
                INTERFACE_AGGREGATE(Exchange::IDeviceAudioCapabilities, _deviceAudioCapabilities)
                INTERFACE_AGGREGATE(Exchange::IDeviceVideoCapabilities, _deviceVideoCapabilities)
                END_INTERFACE_MAP

                //  IPlugin methods
                // -------------------------------------------------------------------------------------------------------
                const string Initialize(PluginHost::IShell* service) override;
                void Deinitialize(PluginHost::IShell* service) override;
                string Information() const override;

            private:
                void Deactivated(RPC::IRemoteConnection* connection);

            private:
                PluginHost::IShell* _service{};
                uint32_t _connectionId{};
                Exchange::IDeviceInfo* _deviceInfo{};
                Exchange::IDeviceAudioCapabilities* _deviceAudioCapabilities{};
                Exchange::IDeviceVideoCapabilities* _deviceVideoCapabilities{};
                Exchange::IConfiguration* configure;
       };
    } // namespace Plugin
} // namespace WPEFramework
