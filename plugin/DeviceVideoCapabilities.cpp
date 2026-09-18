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

#include "DeviceVideoCapabilities.h"

#include "DeviceSettingsInterface.h"
#include <interfaces/IDeviceSettingsHost.h>    // Exchange::IDeviceSettingsHost (GetEDID)
#include <sstream>

namespace WPEFramework {
namespace Plugin {

    void DeviceVideoCapabilities::OnDeviceSettingsActivated()
    {
        // Config is loaded lazily by DSHelper::_ensureConfigLoaded() on the first
        // accessor call. No explicit load needed here.
        LOGINFO("DeviceVideoCapabilities: DeviceSettings activated");
    }

    void DeviceVideoCapabilities::OnDeviceSettingsDeactivated()
    {
        // DSHelper::Operational(false) already clears all config stores and handles.
        LOGINFO("DeviceVideoCapabilities: DeviceSettings deactivated");
    }

    SERVICE_REGISTRATION(DeviceVideoCapabilities, 1, 0);

    DeviceVideoCapabilities::DeviceVideoCapabilities()
    {
    }

    DeviceVideoCapabilities::~DeviceVideoCapabilities()
    {
        DSHelper::Close();
    }

    uint32_t DeviceVideoCapabilities::Configure(PluginHost::IShell* service)
    {
        DSHelper::Open(service, "DeviceVideoCaps");
        return Core::ERROR_NONE;
    }

    Core::hresult DeviceVideoCapabilities::SupportedVideoDisplays(RPC::IStringIterator*& supportedVideoDisplays, bool& success) const
    {
        uint32_t result = Core::ERROR_NONE;

        std::list<string> list;

        // Read from cached config via DSHelper — no COM-RPC round-trip needed for static port enumeration
        std::vector<VideoPortEntry> entries;
        if (!DSHelper::getVideoPortEntries(entries)) {
            LOGERR("SupportedVideoDisplays: DeviceSettings config not available");
            return Core::ERROR_UNAVAILABLE;
        }
        for (size_t i = 0; i < entries.size(); ++i) {
            const string& name = entries[i].name;
            if (std::find(list.begin(), list.end(), name) == list.end()) {
                list.emplace_back(name);
            }
        }

        if (result == Core::ERROR_NONE) {
            supportedVideoDisplays = (Core::Service<RPC::StringIterator>::Create<RPC::IStringIterator>(list));
            success = true;
        }

        return result;
    }

    Core::hresult DeviceVideoCapabilities::HostEDID(HostEdid& hostEdid) const
    {
        uint32_t result = Core::ERROR_NONE;

        // COM-RPC path: use IDeviceSettingsHost::GetEDID (maps device::Host::getHostEDID).
        // Standard EDID is 128 bytes (base) or 256 bytes (with 1 extension block).
        // Allocate 256 bytes; trailing zeros are trimmed before base64 encoding.
        auto* host = AcquireSubInterface<Exchange::IDeviceSettingsHost>();
        if (!host) {
            LOGERR("HostEDID: DeviceSettings host interface not available");
            return Core::ERROR_UNAVAILABLE;
        }

        static const uint16_t kEdidBufLen = 256;
        std::vector<uint8_t> edidBuf(kEdidBufLen, 0);
        result = host->GetEDID(edidBuf.data(), kEdidBufLen);
        host->Release();

        if (result == Core::ERROR_NONE) {
            // Trim trailing zero-padding to find the actual EDID size
            size_t actualLen = kEdidBufLen;
            while (actualLen > 0 && edidBuf[actualLen - 1] == 0) {
                actualLen--;
            }
            if (actualLen == 0) actualLen = kEdidBufLen; // safety: keep full buffer

            if (actualLen > static_cast<size_t>(std::numeric_limits<uint16_t>::max())) {
                result = Core::ERROR_GENERAL;
            } else {
                string base64String;
                Core::ToString(edidBuf.data(), static_cast<uint16_t>(actualLen), true, base64String);
                hostEdid.EDID = std::move(base64String);
            }
        }

        return result;
    }

    Core::hresult DeviceVideoCapabilities::DefaultResolution(const string& videoDisplay, DefaultResln& defaultResln) const
    {
        uint32_t result = Core::ERROR_NONE;

        // Read from cached config via DSHelper — no COM-RPC round-trip needed
        const string portName = videoDisplay.empty() ? DSHelper::getDefaultVideoPortName() : videoDisplay;
        const string res = DSHelper::getVideoPortDefaultResolution(portName);
        if (res.empty()) {
            result = Core::ERROR_NOT_EXIST;
        } else {
            defaultResln.defaultResolution = res;
        }

        return result;
    }

    Core::hresult DeviceVideoCapabilities::SupportedResolutions(const string& videoDisplay, RPC::IStringIterator*& supportedResolutions, bool& success) const
    {
        uint32_t result = Core::ERROR_NONE;

        std::list<string> list;

        // Read from cached config via DSHelper — no COM-RPC round-trip needed
        const string portName = videoDisplay.empty() ? DSHelper::getDefaultVideoPortName() : videoDisplay;
        VideoPortEntry resolvedEntry;
        if (!DSHelper::resolveVideoPortByName(portName, resolvedEntry)) {
            result = Core::ERROR_NOT_EXIST;
        } else {
            VideoPortTypeConfig typeConfig;
            if (DSHelper::getVideoPortTypeConfig(resolvedEntry.type, typeConfig)) {
                // Parse comma-separated list of supported resolution names
                std::istringstream ss(typeConfig.supportedResolutionNames);
                string token;
                while (std::getline(ss, token, ',')) {
                    if (!token.empty()) {
                        list.emplace_back(token);
                    }
                }
            }
        }

        if (result == Core::ERROR_NONE) {
            supportedResolutions = (Core::Service<RPC::StringIterator>::Create<RPC::IStringIterator>(list));
            success = true;
        }

        return result;
    }

    Core::hresult DeviceVideoCapabilities::SupportedHdcp(const string& videoDisplay, SupportedHDCPVer& supportedHDCPVer) const
    {
        uint32_t result = Core::ERROR_NONE;

        // Use cached config via DSHelper for port name resolution — only HDCP version query needs COM-RPC
        const string portName = videoDisplay.empty() ? DSHelper::getDefaultVideoPortName() : videoDisplay;
        VideoPortEntry resolvedEntry;
        if (!DSHelper::resolveVideoPortByName(portName, resolvedEntry)) {
            return Core::ERROR_NOT_EXIST;
        }
        // Use the cached video port handle acquired during config loading
        const int32_t handle = DSHelper::getCachedVideoPortHandle(resolvedEntry.name);
        if (handle == INVALID_DS_HANDLE) {
            LOGERR("SupportedHdcp: video port handle not available for '%s'", resolvedEntry.name.c_str());
            return Core::ERROR_UNAVAILABLE;
        }
        auto* vp = AcquireSubInterface<Exchange::IDeviceSettingsVideoPort>();
        if (!vp) {
            LOGERR("SupportedHdcp: IDeviceSettingsVideoPort interface not available");
            return Core::ERROR_UNAVAILABLE;
        }
        Exchange::IDeviceSettingsVideoPort::HDCPProtocolVersion version;
        result = vp->GetHDCPProtocolVersionOnVideoPort(handle, version);
        if (result == Core::ERROR_NONE) {
            switch (version) {
            case Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_2X:
                supportedHDCPVer.supportedHDCPVersion = HDCP_22;
                break;
            case Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_1X:
                supportedHDCPVer.supportedHDCPVersion = HDCP_14;
                break;
            default:
                result = Core::ERROR_GENERAL;
                break;
            }
        }
        vp->Release();
        return result;
    }
}
}
