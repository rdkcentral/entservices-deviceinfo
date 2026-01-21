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

#include "DeviceInfoImplementation.h"

#include "mfrMgr.h"
#include "rfcapi.h"
#include "secure_wrapper.h"
#include "exception.hpp"
#include "host.hpp"
#include "manager.hpp"
#include "UtilsIarm.h"

#include <fstream>
#include <regex>

namespace WPEFramework {
namespace Plugin {
    namespace {

        uint32_t GetFileRegex(const char* filename, const std::regex& regex, string& response)
        {
            uint32_t result = Core::ERROR_GENERAL;

            std::ifstream file(filename);
            if (file) {
                string line;
                while (std::getline(file, line)) {
                    std::smatch sm;
                    if (std::regex_match(line, sm, regex)) {
                        ASSERT(sm.size() == 2);
                        response = sm[1];
                        result = Core::ERROR_NONE;
                        break;
                    }
                }
            }

            return result;
        }

        uint32_t GetMFRData(mfrSerializedType_t type, string& response)
        {
            uint32_t result = Core::ERROR_GENERAL;

            IARM_Bus_MFRLib_GetSerializedData_Param_t param;
            param.bufLen = 0;
            param.type = type;
            auto status = IARM_Bus_Call(
                IARM_BUS_MFRLIB_NAME, IARM_BUS_MFRLIB_API_GetSerializedData, &param, sizeof(param));
            if ((status == IARM_RESULT_SUCCESS) && param.bufLen) {
                response.assign(param.buffer, param.bufLen);
                result = Core::ERROR_NONE;
            } else {
                TRACE_GLOBAL(Trace::Information, (_T("MFR error [%d] for %d"), status, type));
            }

            return result;
        }

        uint32_t GetRFCData(const char* name, string& response)
        {
            uint32_t result = Core::ERROR_GENERAL;

            RFC_ParamData_t param;
            auto status = getRFCParameter(nullptr, name, &param);
            if ((status == WDMP_SUCCESS) && param.value[0]) {
                response = param.value;
                result = Core::ERROR_NONE;
            } else {
                TRACE_GLOBAL(Trace::Information, (_T("RFC error [%d] for %s"), status, name));
            }

            return result;
        }
        
    }

    SERVICE_REGISTRATION(DeviceInfoImplementation, 1, 0);

    DeviceInfoImplementation::DeviceInfoImplementation():_service(nullptr)
    {
        Utils::IARM::init();
        try {
            device::Manager::Initialize();
        } catch (const device::Exception& e) {
            TRACE(Trace::Fatal, (_T("Exception caught %s"), e.what()));
        } catch (const std::exception& e) {
            TRACE(Trace::Fatal, (_T("Exception caught %s"), e.what()));
        } catch (...) {
        }
    }

    DeviceInfoImplementation::~DeviceInfoImplementation()
    {
        LOGINFO("DeviceInfoImplementation destructor");
        if (_service != nullptr)
        {
            _service->Release();
            _service = nullptr;
        }
    }

    uint32_t DeviceInfoImplementation::Configure(PluginHost::IShell* service)
    {
        LOGINFO("Configuring DeviceInfoImplementation");
        ASSERT(service != nullptr);
        _service = service;
        _service->AddRef();

        return Core::ERROR_NONE;
    }

    Core::hresult DeviceInfoImplementation::SerialNumber(DeviceSerialNo& deviceSerialNo) const
    {
        return (GetMFRData(mfrSERIALIZED_TYPE_SERIALNUMBER, deviceSerialNo.serialnumber)
                   == Core::ERROR_NONE)
            ? Core::ERROR_NONE
            : GetRFCData(_T("Device.DeviceInfo.SerialNumber"), deviceSerialNo.serialnumber);
    }

    Core::hresult DeviceInfoImplementation::Sku(DeviceModelNo& deviceModelNo) const
    {
        return (GetFileRegex(_T("/etc/device.properties"),
                    std::regex("^MODEL_NUM(?:\\s*)=(?:\\s*)(?:\"{0,1})([^\"\\n]+)(?:\"{0,1})(?:\\s*)$"), deviceModelNo.sku)
                   == Core::ERROR_NONE)
            ? Core::ERROR_NONE
            : ((GetMFRData(mfrSERIALIZED_TYPE_MODELNAME, deviceModelNo.sku)
                   == Core::ERROR_NONE)
                    ? Core::ERROR_NONE
                    : GetRFCData(_T("Device.DeviceInfo.ModelName"), deviceModelNo.sku));
    }

    Core::hresult DeviceInfoImplementation::Make(DeviceMake& deviceMake) const
    {
        return ( GetMFRData(mfrSERIALIZED_TYPE_MANUFACTURER, deviceMake.make) == Core::ERROR_NONE)
            ? Core::ERROR_NONE
            : GetFileRegex(_T("/etc/device.properties"),std::regex("^MFG_NAME(?:\\s*)=(?:\\s*)(?:\"{0,1})([^\"\\n]+)(?:\"{0,1})(?:\\s*)$"), deviceMake.make);
    }

    Core::hresult DeviceInfoImplementation::Model(DeviceModel& deviceModel) const
    {
        return
#ifdef ENABLE_DEVICE_MANUFACTURER_INFO
            (GetMFRData(mfrSERIALIZED_TYPE_PROVISIONED_MODELNAME, deviceModel.model) == Core::ERROR_NONE)
            ? Core::ERROR_NONE
            :
#endif
            GetFileRegex(_T("/etc/device.properties"),
                std::regex("^FRIENDLY_ID(?:\\s*)=(?:\\s*)(?:\"{0,1})([^\"\\n]+)(?:\"{0,1})(?:\\s*)$"), deviceModel.model);
    }

    Core::hresult DeviceInfoImplementation::Brand(DeviceBrand& deviceBrand) const
    {
        deviceBrand.brand = "Unknown";
        return
            ((Core::ERROR_NONE == GetFileRegex(_T("/tmp/.manufacturer"), std::regex("^([^\\n]+)$"), deviceBrand.brand)) || 
             (GetMFRData(mfrSERIALIZED_TYPE_MANUFACTURER, deviceBrand.brand) == Core::ERROR_NONE))?Core::ERROR_NONE:Core::ERROR_GENERAL;
    }

    Core::hresult DeviceInfoImplementation::DeviceType(DeviceTypeInfos& deviceTypeInfos) const
    {
        const char* device_type;
        string deviceTypeInfo;
        uint32_t result = GetFileRegex(_T("/etc/authService.conf"),
            std::regex("^deviceType(?:\\s*)=(?:\\s*)(?:\"{0,1})([^\"\\n]+)(?:\"{0,1})(?:\\s*)$"), deviceTypeInfo);

        if (result != Core::ERROR_NONE) {
            // If we didn't find the deviceType in authService.conf, try device.properties
            result = GetFileRegex(_T("/etc/device.properties"),
                std::regex("^DEVICE_TYPE(?:\\s*)=(?:\\s*)(?:\"{0,1})([^\"\\n]+)(?:\"{0,1})(?:\\s*)$"), deviceTypeInfo);

            if (result == Core::ERROR_NONE) {
                // Perform the conversion logic if we found the deviceType in device.properties
                // as it doesnt comply with plugin spec. See RDKEMW-276
                device_type = deviceTypeInfo.c_str();
                deviceTypeInfo = (strcmp("mediaclient", device_type) == 0) ? "IpStb" :
                    (strcmp("hybrid", device_type) == 0) ? "QamIpStb" : "IpTv";
            }
        }

        static const std::unordered_map<std::string, DeviceTypeInfo> stringToDeviceType = {
            {"IpTv",        DEVICE_TYPE_IPTV},
            {"IpStb",       DEVICE_TYPE_IPSTB},
            {"QamIpStb",    DEVICE_TYPE_QAMIPSTB}
        };

        auto it = stringToDeviceType.find(deviceTypeInfo);
        if (it != stringToDeviceType.end()) {
            deviceTypeInfos.devicetype = it->second;
        }
        return result;
    }


    Core::hresult DeviceInfoImplementation::SocName(DeviceSoc& deviceSoc)  const
    {
        return (GetFileRegex(_T("/etc/device.properties"),
                std::regex("^SOC(?:\\s*)=(?:\\s*)(?:\"{0,1})([^\"\\n]+)(?:\"{0,1})(?:\\s*)$"), deviceSoc.socname));
    }

    Core::hresult DeviceInfoImplementation::DistributorId(DeviceDistId& deviceDistId) const
    {
        return (GetFileRegex(_T("/opt/www/authService/partnerId3.dat"),
                    std::regex("^([^\\n]+)$"), deviceDistId.distributorid)
                   == Core::ERROR_NONE)
            ? Core::ERROR_NONE
            : GetRFCData(_T("Device.DeviceInfo.X_RDKCENTRAL-COM_Syndication.PartnerId"), deviceDistId.distributorid);
    }

    Core::hresult DeviceInfoImplementation::ReleaseVersion(DeviceReleaseVer& deviceReleaseVer) const
    {
        const std::string defaultVersion = "99.99.0.0";
        std::regex pattern(R"((\d+)\.(\d+)[sp])");
        std::smatch match;
        std::string imagename = "";
        if(Core::ERROR_NONE == GetFileRegex(_T("/version.txt"), std::regex("^imagename:([^\\n]+)$"), imagename))
        {
            if (std::regex_search(imagename, match, pattern)) {
                std::string major = match[1];
                std::string minor = match[2];
                deviceReleaseVer.releaseversion = major + "." + minor + ".0.0";
            }
            else
            {
                deviceReleaseVer.releaseversion = std::move(defaultVersion);
                LOGERR("Unable to get releaseVersion of the Image:%s.So default releaseVersion is: %s ",imagename.c_str(),deviceReleaseVer.releaseversion.c_str());
            }
        }
        else
        {
                deviceReleaseVer.releaseversion = std::move(defaultVersion);
                LOGERR("Unable to read from /version.txt So default releaseVersion is: %s ",deviceReleaseVer.releaseversion.c_str());

        }

        return Core::ERROR_NONE;
    }

    Core::hresult DeviceInfoImplementation::ChipSet(DeviceChip& deviceChip) const
    {
        auto result = GetFileRegex(_T("/etc/device.properties"),std::regex("^CHIPSET_NAME(?:\\s*)=(?:\\s*)(?:\"{0,1})([^\"\\n]+)(?:\"{0,1})(?:\\s*)$"), deviceChip.chipset);
        return result;
    }

    Core::hresult DeviceInfoImplementation::FirmwareVersion(FirmwareversionInfo& firmwareVersionInfo) const
    {
        uint32_t result = Core::ERROR_GENERAL;

        result = GetFileRegex(_T("/version.txt"), std::regex("^imagename:([^\\n]+)$"), firmwareVersionInfo.imagename);
        
        if (result == Core::ERROR_NONE )
        {
            if (GetFileRegex(_T("/version.txt"), std::regex("^SDK_VERSION=([^\\n]+)$"), firmwareVersionInfo.sdk) != Core::ERROR_NONE)
            {
                firmwareVersionInfo.sdk = "";
            }

            if (GetFileRegex(_T("/version.txt"), std::regex("^MEDIARITE=([^\\n]+)$"), firmwareVersionInfo.mediarite) != Core::ERROR_NONE)
            {
                firmwareVersionInfo.mediarite = "";
            }

            if (GetFileRegex(_T("/version.txt"), std::regex("^YOCTO_VERSION=([^\\n]+)$"), firmwareVersionInfo.yocto) != Core::ERROR_NONE)
            {
                firmwareVersionInfo.yocto = "";
            }

            if (GetMFRData(mfrSERIALIZED_TYPE_PDRIVERSION, firmwareVersionInfo.pdri) != Core::ERROR_NONE)
            {
                firmwareVersionInfo.pdri = "";
            }
        }
        
        return result;
    }

    Core::hresult DeviceInfoImplementation::SystemInfo(SystemInfos& systemInfo) const
    {
        DeviceSerialNo deviceSerial;
        struct timespec currentTime{};

        PluginHost::ISubSystem* _subSystem = nullptr;
        _subSystem = _service->SubSystems();
        ASSERT(_subSystem != nullptr);
        SerialNumber(deviceSerial);
        Core::SystemInfo& singleton(Core::SystemInfo::Instance());
        clock_gettime(CLOCK_REALTIME, &currentTime);
        systemInfo.time = Core::Time(currentTime).ToRFC1123(true);
        if (_subSystem != nullptr) {
            systemInfo.version = _subSystem->Version() + _T("#") + _subSystem->BuildTreeHash();
        }
        systemInfo.uptime = singleton.GetUpTime();
        systemInfo.freeram = singleton.GetFreeRam();
        systemInfo.totalram = singleton.GetTotalRam();
        systemInfo.memunit = singleton.GetMemUnit();
        systemInfo.totalswap = singleton.GetTotalSwap();
        systemInfo.freeswap = singleton.GetFreeSwap();
        systemInfo.devicename = singleton.GetHostName();
        systemInfo.cpuload = Core::NumberType<uint32_t>(static_cast<uint32_t>(singleton.GetCpuLoad())).Text();
        systemInfo.serialnumber = deviceSerial.serialnumber;
        auto cpuloadavg = singleton.GetCpuLoadAvg();
        if (cpuloadavg != nullptr) {
            systemInfo.cpuloadavg.avg1min = *(cpuloadavg);
            if (++cpuloadavg != nullptr) {
                systemInfo.cpuloadavg.avg5min = *(cpuloadavg);
                if (++cpuloadavg != nullptr) {
                    systemInfo.cpuloadavg.avg15min = *(cpuloadavg);
                }
            }
        }

        if (_subSystem != nullptr) {
            _subSystem->Release();
        }

        return Core::ERROR_NONE;
    }

    Core::hresult DeviceInfoImplementation::Addresses(IAddressesInfoIterator*& addressesInfo) const
    {
        std::list<AddressesInfo> deviceAddressesInfoList;
        AddressesInfo deviceAddressInfo;
        Core::JSON::String nodeName;
        Core::AdapterIterator interfaces;

        while (interfaces.Next() == true) {

            deviceAddressInfo.name = interfaces.Name();
            deviceAddressInfo.mac = interfaces.MACAddress(':');

            // get an interface with a public IP address, then we will have a proper MAC address..
            Core::IPV4AddressIterator selectedNode(interfaces.IPV4Addresses());

            while (selectedNode.Next() == true) {
                nodeName = selectedNode.Address().HostAddress();
            }

            deviceAddressInfo.ip = nodeName.Value();

            deviceAddressesInfoList.push_back(deviceAddressInfo);

        }
        
        addressesInfo = Core::Service<RPC::IteratorType<Exchange::IDeviceInfo::IAddressesInfoIterator>> \
                                    ::Create<Exchange::IDeviceInfo::IAddressesInfoIterator>(deviceAddressesInfoList);

        return Core::ERROR_NONE;
    }

    Core::hresult DeviceInfoImplementation::EthMac(EthernetMac& ethernetMac) const
    {
        FILE* fp = v_secure_popen("r", "/lib/rdk/getDeviceDetails.sh read eth_mac");
        if (!fp) {
            return Core::ERROR_GENERAL;
    	}

        std::ostringstream oss;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), fp) != nullptr) {
            oss << buffer;
        }
        v_secure_pclose(fp);

        ethernetMac.ethMac = oss.str();

        // Remove trailing newline if present
        if (!ethernetMac.ethMac.empty() && ethernetMac.ethMac.back() == '\n') {
             ethernetMac.ethMac.pop_back();
        }

        return Core::ERROR_NONE;
    }

    Core::hresult DeviceInfoImplementation::EstbMac(StbMac& stbMac) const
    {
        FILE* fp = v_secure_popen("r", "/lib/rdk/getDeviceDetails.sh read estb_mac");
        if (!fp) {
                return Core::ERROR_GENERAL;
        }

        std::ostringstream oss;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), fp) != nullptr) {
                oss << buffer;
        }
        v_secure_pclose(fp);

        stbMac.estbMac = oss.str();

        // Remove trailing newline if present
        if (!stbMac.estbMac.empty() && stbMac.estbMac.back() == '\n') {
                stbMac.estbMac.pop_back();
        }

        return Core::ERROR_NONE;
    }
 
    Core::hresult DeviceInfoImplementation::WifiMac(WiFiMac& wiFiMac) const
    {
        FILE* fp = v_secure_popen("r", "/lib/rdk/getDeviceDetails.sh read wifi_mac");
        if (!fp) {
                return Core::ERROR_GENERAL;
        }

        std::ostringstream oss;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), fp) != nullptr) {
                oss << buffer;
        }
        v_secure_pclose(fp);

        wiFiMac.wifiMac = oss.str();
 
        // Remove trailing newline if present
        if (!wiFiMac.wifiMac.empty() && wiFiMac.wifiMac.back() == '\n') {
                wiFiMac.wifiMac.pop_back();
        }

        return Core::ERROR_NONE;
    }

    Core::hresult DeviceInfoImplementation::EstbIp(StbIp& stbIp) const
    {
        FILE* fp = v_secure_popen("r", "/lib/rdk/getDeviceDetails.sh read estb_ip");
        if (!fp) {
                return Core::ERROR_GENERAL;
        }

        std::ostringstream oss;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), fp) != nullptr) {
                oss << buffer;
        }
        v_secure_pclose(fp);

        stbIp.estbIp = oss.str();

        // Remove trailing newline if present
        if (!stbIp.estbIp.empty() && stbIp.estbIp.back() == '\n') {
                stbIp.estbIp.pop_back();
        }

        return Core::ERROR_NONE;
    }

    Core::hresult DeviceInfoImplementation::SupportedAudioPorts(RPC::IStringIterator*& supportedAudioPorts, bool& success) const
    {
        uint32_t result = Core::ERROR_NONE;

        std::list<string> list;

        try {
            const auto& aPorts = device::Host::getInstance().getAudioOutputPorts();
            for (size_t i = 0; i < aPorts.size(); i++) {
                list.emplace_back(aPorts.at(i).getName());
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

        if (result == Core::ERROR_NONE) {
            supportedAudioPorts = (Core::Service<RPC::StringIterator>::Create<RPC::IStringIterator>(list));
            success = true;
        }

        return result;
    }
}
}
