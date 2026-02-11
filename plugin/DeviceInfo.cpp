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

#include "DeviceInfo.h"

#define API_VERSION_NUMBER_MAJOR 1
#define API_VERSION_NUMBER_MINOR 0
#define API_VERSION_NUMBER_PATCH 0

namespace WPEFramework
{

    namespace {

        static Plugin::Metadata<Plugin::DeviceInfo> metadata(
            // Version (Major, Minor, Patch)
            API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH,
            // Preconditions
            {},
            // Terminations
            {},
            // Controls
            {}
        );
    }

    namespace Plugin
    {

        /*
         *Register DeviceInfo module as wpeframework plugin
         **/
        SERVICE_REGISTRATION(DeviceInfo, API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH);

    DeviceInfo::DeviceInfo() : _service(nullptr), _connectionId(0), _deviceInfo(nullptr), _deviceAudioCapabilities(nullptr), _deviceVideoCapabilities(nullptr), configure(nullptr)
    {
        SYSLOG(Logging::Startup, (_T("DeviceInfo Constructor")));
    }

    DeviceInfo::~DeviceInfo()
    {
        SYSLOG(Logging::Shutdown, (string(_T("DeviceInfo Destructor"))));
    }

    const string DeviceInfo::Initialize(PluginHost::IShell* service)
    {
        string message ="";
        ASSERT(nullptr != service);
        ASSERT(nullptr == _service);
        ASSERT(nullptr == _deviceInfo);
        ASSERT(0 == _connectionId);

        SYSLOG(Logging::Startup, (_T("DeviceInfo::Initialize: PID=%u"), getpid()));

        _service = service;
        _service->AddRef();

        _deviceInfo = _service->Root<Exchange::IDeviceInfo>(_connectionId, 2000, _T("DeviceInfoImplementation"));
        _deviceAudioCapabilities = service->Root<Exchange::IDeviceAudioCapabilities>(_connectionId, 2000, _T("DeviceAudioCapabilities"));
        _deviceVideoCapabilities = service->Root<Exchange::IDeviceVideoCapabilities>(_connectionId, 2000, _T("DeviceVideoCapabilities"));

        ASSERT(_deviceInfo != nullptr);
        ASSERT(_deviceAudioCapabilities != nullptr);
        ASSERT(_deviceVideoCapabilities != nullptr);

        if(nullptr != _deviceInfo && nullptr != _deviceAudioCapabilities && nullptr != _deviceVideoCapabilities)
        {
            configure = _deviceInfo->QueryInterface<Exchange::IConfiguration>();
            if (configure != nullptr)
            {
                uint32_t result = configure->Configure(_service);
                if(result != Core::ERROR_NONE)
                {
                    message = _T("DeviceInfo could not be configured");
                }
            }
            else
            {
                message = _T("DeviceInfo implementation did not provide a configuration interface");
            }
            // Invoking Plugin API register to wpeframework
            Exchange::JDeviceInfo::Register(*this, _deviceInfo);
            Exchange::JDeviceAudioCapabilities::Register(*this, _deviceAudioCapabilities);
            Exchange::JDeviceVideoCapabilities::Register(*this, _deviceVideoCapabilities);
        }
        else
        {
            SYSLOG(Logging::Startup, (_T("DeviceInfo::Initialize: Failed to initialise DeviceInfo plugin")));
            message = _T("DeviceInfo plugin could not be initialised");
        }

        // On success return empty, to indicate there is no error text.
        return message;
    }

    void DeviceInfo::Deinitialize(PluginHost::IShell* service)
    {
        ASSERT(_service == service);
        ASSERT(_deviceInfo != nullptr);
        ASSERT(_deviceAudioCapabilities != nullptr);
        ASSERT(_deviceVideoCapabilities != nullptr);

        SYSLOG(Logging::Shutdown, (string(_T("DeviceInfo::Deinitialize"))));

        if (nullptr != _deviceInfo && nullptr != _deviceAudioCapabilities && nullptr != _deviceVideoCapabilities)
        {
            Exchange::JDeviceAudioCapabilities::Unregister(*this);
            _deviceAudioCapabilities->Release();
            _deviceAudioCapabilities = nullptr;

            Exchange::JDeviceVideoCapabilities::Unregister(*this);
            _deviceVideoCapabilities->Release();
            _deviceVideoCapabilities = nullptr;

            Exchange::JDeviceInfo::Unregister(*this);

            configure->Release();

            // Stop processing:
            RPC::IRemoteConnection* connection = service->RemoteConnection(_connectionId);
            VARIABLE_IS_NOT_USED uint32_t result = _deviceInfo->Release();

            _deviceInfo = nullptr;

            // It should have been the last reference we are releasing,
            // so it should endup in a DESTRUCTION_SUCCEEDED, if not we
            // are leaking...
            ASSERT(result == Core::ERROR_DESTRUCTION_SUCCEEDED);

            // If this was running in a (container) process...
            if (nullptr != connection)
            {
                // Lets trigger the cleanup sequence for
                // out-of-process code. Which will guard
                // that unwilling processes, get shot if
                // not stopped friendly :-)
                try
                {
                    connection->Terminate();
                    // Log success if needed
                    LOGWARN("Connection terminated successfully.");
                }
                catch (const std::exception& e)
                {
                    std::string errorMessage = "Failed to terminate connection: ";
                    errorMessage += e.what();
                    LOGWARN("%s",errorMessage.c_str());
               }

               connection->Release();
            }
        }

        _connectionId = 0;
        _service->Release();
        _service = nullptr;
        SYSLOG(Logging::Shutdown, (string(_T("Deviceinfo de-initialised"))));
    }

    string DeviceInfo::Information() const
    {
        return "The DeviceInfo plugin allows retrieving of various device-related information.";
    }

    void DeviceInfo::Deactivated(RPC::IRemoteConnection* connection)
    {
        if (connection->Id() == _connectionId) {
            ASSERT(nullptr != _service);
            Core::IWorkerPool::Instance().Submit(PluginHost::IShell::Job::Create(_service, PluginHost::IShell::DEACTIVATED, PluginHost::IShell::FAILURE));
        }
    }
} // namespace Plugin
} // namespace WPEFramework
