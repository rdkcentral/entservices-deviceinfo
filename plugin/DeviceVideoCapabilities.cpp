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

#ifdef USE_DEVICESETTING_PLUGIN
#include "DeviceSettingsInterface.h"
#include <sstream>
#else
#include "exception.hpp"
#include "host.hpp"
#include "manager.hpp"
#include "videoOutputPortConfig.hpp"

#include "UtilsIarm.h"
#endif

namespace WPEFramework {
namespace Plugin {

#ifdef USE_DEVICESETTING_PLUGIN
    void DeviceVideoCapabilities::OnDeviceSettingsActivated()
    {
        LOGINFO("DeviceSettingsActivated: loading video port config");
        auto* vp = AcquireSubInterface<Exchange::IDeviceSettingsVideoPort>();
        if (vp) {
            LoadVideoPortConfig(vp, _videoPortConfig);
            vp->Release();
        } else {
            LOGERR("OnDeviceSettingsActivated: IDeviceSettingsVideoPort not available");
        }
    }

    void DeviceVideoCapabilities::OnDeviceSettingsDeactivated()
    {
        LOGINFO("DeviceSettingsDeactivated: clearing video port config");
        _videoPortConfig.Clear();
    }
#endif

    SERVICE_REGISTRATION(DeviceVideoCapabilities, 1, 0);

    DeviceVideoCapabilities::DeviceVideoCapabilities()
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

    DeviceVideoCapabilities::~DeviceVideoCapabilities()
    {
#ifdef USE_DEVICESETTING_PLUGIN
        DeviceSettingsClientHelper::Close();
#endif
    }

#ifdef USE_DEVICESETTING_PLUGIN
    uint32_t DeviceVideoCapabilities::Configure(PluginHost::IShell* service)
    {
        DeviceSettingsClientHelper::Open(service);
        return Core::ERROR_NONE;
    }
#endif

    Core::hresult DeviceVideoCapabilities::SupportedVideoDisplays(RPC::IStringIterator*& supportedVideoDisplays, bool& success) const
    {
        uint32_t result = Core::ERROR_NONE;

        std::list<string> list;

#ifdef USE_DEVICESETTING_PLUGIN
        // Read from cached config — no COM-RPC round-trip needed for static port enumeration
        if (_videoPortConfig.IsEmpty()) {
            LOGERR("SupportedVideoDisplays: DeviceSettings config not available");
            return Core::ERROR_UNAVAILABLE;
        }
        std::vector<VideoPortEntry> entries;
        _videoPortConfig.BuildVideoPortEntries(entries);
        for (size_t i = 0; i < entries.size(); ++i) {
            const string& name = entries[i].name;
            if (std::find(list.begin(), list.end(), name) == list.end()) {
                list.emplace_back(name);
            }
        }
#else
        try {
            const auto& vPorts = device::Host::getInstance().getVideoOutputPorts();
            for (size_t i = 0; i < vPorts.size(); i++) {

                /**
                 * There's N:1 relation between VideoOutputPort and AudioOutputPort.
                 * When there are multiple Audio Ports on the Video Port,
                 * there are multiple VideoOutputPort-s as well.
                 * Those VideoOutputPort-s are the same except holding a different Audio Port id.
                 * As a result, a list of Video Ports has multiple Video Ports
                 * that represent the same Video Port, but different Audio Port.
                 * A list of VideoOutputPort-s returned from DS
                 * needs to be filtered by name.
                 */

                auto name = vPorts.at(i).getName();
                if (std::find(list.begin(), list.end(), name) != list.end())
                    continue;

                list.emplace_back(name);
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
            supportedVideoDisplays = (Core::Service<RPC::StringIterator>::Create<RPC::IStringIterator>(list));
            success = true;
        }

        return result;
    }

    Core::hresult DeviceVideoCapabilities::HostEDID(HostEdid& hostEdid) const
    {
        uint32_t result = Core::ERROR_NONE;

#ifdef USE_DEVICESETTING_PLUGIN
        // COM-RPC path: use IDeviceSettingsHost::GetEDID (maps device::Host::getHostEDID).
        // Standard EDID is 128 bytes (base) or 256 bytes (with 1 extension block).
        // Allocate 256 bytes; trailing zeros are trimmed before base64 encoding.
        auto* host = AcquireSubInterfaceMutable<Exchange::IDeviceSettingsHost>();
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
#else
        std::vector<uint8_t> edidVec({ 'u', 'n', 'k', 'n', 'o', 'w', 'n' });
        try {
            std::vector<unsigned char> edidVec2;
            device::Host::getInstance().getHostEDID(edidVec2);
            edidVec = std::move(edidVec2);
        } catch (const device::Exception& e) {
            TRACE(Trace::Fatal, (_T("Exception caught %s"), e.what()));
            result = Core::ERROR_GENERAL;
        } catch (const std::exception& e) {
            TRACE(Trace::Fatal, (_T("Exception caught %s"), e.what()));
            result = Core::ERROR_GENERAL;
        } catch (...) {
            result = Core::ERROR_GENERAL;
        }

        if (result == Core::ERROR_NONE) {
            // convert to base64

            if (edidVec.size() > (size_t)std::numeric_limits<uint16_t>::max()) {
                result = Core::ERROR_GENERAL;
            } else {
                string base64String;
                Core::ToString((uint8_t*)&edidVec[0], edidVec.size(), true, base64String);
                hostEdid.EDID = std::move(base64String);
            }
        }
#endif

        return result;
    }

    Core::hresult DeviceVideoCapabilities::DefaultResolution(const string& videoDisplay, DefaultResln& defaultResln) const
    {
        uint32_t result = Core::ERROR_NONE;

#ifdef USE_DEVICESETTING_PLUGIN
        // Read from cached config — no COM-RPC round-trip needed
        if (_videoPortConfig.IsEmpty()) {
            LOGERR("DefaultResolution: DeviceSettings config not available");
            return Core::ERROR_UNAVAILABLE;
        }
        const string portName = videoDisplay.empty() ? _videoPortConfig.GetDefaultVideoPortName() : videoDisplay;
        const string res = _videoPortConfig.GetDefaultResolution(portName);
        if (res.empty()) {
            result = Core::ERROR_NOT_EXIST;
        } else {
            defaultResln.defaultResolution = res;
        }
#else
        try {
            auto strVideoPort = videoDisplay.empty() ? device::Host::getInstance().getDefaultVideoPortName() : videoDisplay;
            auto& vPort = device::Host::getInstance().getVideoOutputPort(strVideoPort);
            defaultResln.defaultResolution = vPort.getDefaultResolution().getName();
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

        return result;
    }

    Core::hresult DeviceVideoCapabilities::SupportedResolutions(const string& videoDisplay, RPC::IStringIterator*& supportedResolutions, bool& success) const
    {
        uint32_t result = Core::ERROR_NONE;

        std::list<string> list;

#ifdef USE_DEVICESETTING_PLUGIN
        // Read from cached config — no COM-RPC round-trip needed
        if (_videoPortConfig.IsEmpty()) {
            LOGERR("SupportedResolutions: DeviceSettings config not available");
            return Core::ERROR_UNAVAILABLE;
        }
        const string portName = videoDisplay.empty() ? _videoPortConfig.GetDefaultVideoPortName() : videoDisplay;
        VideoPortEntry resolvedEntry;
        if (!_videoPortConfig.ResolveByName(portName, resolvedEntry)) {
            result = Core::ERROR_NOT_EXIST;
        } else {
            VideoPortTypeConfig typeConfig;
            if (_videoPortConfig.GetTypeConfig(resolvedEntry.type, typeConfig)) {
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
#else
        try {
            auto strVideoPort = videoDisplay.empty() ? device::Host::getInstance().getDefaultVideoPortName() : videoDisplay;
            auto& vPort = device::Host::getInstance().getVideoOutputPort(strVideoPort);
            const auto resolutions = device::VideoOutputPortConfig::getInstance().getPortType(vPort.getType().getId()).getSupportedResolutions();
            for (size_t i = 0; i < resolutions.size(); i++) {
                list.emplace_back(resolutions.at(i).getName());
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
            supportedResolutions = (Core::Service<RPC::StringIterator>::Create<RPC::IStringIterator>(list));
            success = true;
        }

        return result;
    }

    Core::hresult DeviceVideoCapabilities::SupportedHdcp(const string& videoDisplay, SupportedHDCPVer& supportedHDCPVer) const
    {
        uint32_t result = Core::ERROR_NONE;

#ifdef USE_DEVICESETTING_PLUGIN
        // Use cached config for port name resolution — only HDCP version query needs COM-RPC
        if (_videoPortConfig.IsEmpty()) {
            LOGERR("SupportedHdcp: DeviceSettings config not available");
            return Core::ERROR_UNAVAILABLE;
        }
        const string portName = videoDisplay.empty() ? _videoPortConfig.GetDefaultVideoPortName() : videoDisplay;
        VideoPortEntry resolvedEntry;
        if (!_videoPortConfig.ResolveByName(portName, resolvedEntry)) {
            return Core::ERROR_NOT_EXIST;
        }
        auto* vp = AcquireSubInterfaceMutable<Exchange::IDeviceSettingsVideoPort>();
        if (!vp) {
            LOGERR("SupportedHdcp: IDeviceSettingsVideoPort interface not available");
            return Core::ERROR_UNAVAILABLE;
        }
        int32_t handle = -1;
        result = vp->GetVideoPort(resolvedEntry.type, resolvedEntry.index, handle);
        if (result == Core::ERROR_NONE) {
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
        }
        vp->Release();
#else
        try {
            auto strVideoPort = videoDisplay.empty() ? device::Host::getInstance().getDefaultVideoPortName() : videoDisplay;
            auto& vPort = device::VideoOutputPortConfig::getInstance().getPort(strVideoPort);
            switch (vPort.getHDCPProtocol()) {
            case dsHDCP_VERSION_2X:
                supportedHDCPVer.supportedHDCPVersion = HDCP_22;
                break;
            case dsHDCP_VERSION_1X:
                supportedHDCPVer.supportedHDCPVersion = HDCP_14;
                break;
            default:
                result = Core::ERROR_GENERAL;
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
        return result;
    }
}
}
