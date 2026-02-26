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

#include <gtest/gtest.h>

#include "DeviceInfo.h"
#include "DeviceInfoImplementation.h"
#include "DeviceAudioCapabilities.h"
#include "DeviceVideoCapabilities.h"
#include "AudioOutputPortMock.h"
#include "HostMock.h"
#include "IarmBusMock.h"
#include "ManagerMock.h"
#include "ServiceMock.h"
#include "VideoOutputPortConfigMock.h"
#include "VideoOutputPortMock.h"
#include "VideoOutputPortTypeMock.h"
#include "VideoResolutionMock.h"
#include "RfcApiMock.h"
#include "COMLinkMock.h"
#include "DeviceInfoMock.h"
#include "WrapsMock.h"
#include "ISubSystemMock.h"
#include "SystemInfo.h"
#include <fstream>
#include "ThunderPortability.h"

using namespace WPEFramework;

using ::testing::NiceMock;
using ::testing::_;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::Invoke;

namespace {
    const string webPrefix = _T("/Service/DeviceInfo");
    static void removeFile(const char* fileName)
    {
        if (strcmp(fileName, "/etc/device.properties") == 0 || strcmp(fileName, "/etc/authService.conf") == 0 || strcmp(fileName, "/opt/www/authService/partnerId3.dat") == 0 || \
            strcmp(fileName, "/tmp/.manufacturer") == 0 || strcmp(fileName, "/version.txt") == 0) {
            std::ofstream(fileName, std::ios::trunc);
        }
    }
}

class DeviceInfoTest : public ::testing::Test {
protected:
    Core::ProxyType<Plugin::DeviceInfo> plugin;
    Core::ProxyType<Plugin::DeviceInfoImplementation> deviceInfoImplementation;
    Core::ProxyType<Plugin::DeviceAudioCapabilities> deviceAudioCapabilities;
    Core::ProxyType<Plugin::DeviceVideoCapabilities> deviceVideoCapabilities;
    Core::JSONRPC::Handler& handler;
    DECL_CORE_JSONRPC_CONX connection;
    string response;

    IarmBusImplMock* p_iarmBusImplMock = nullptr;
    ManagerImplMock* p_managerImplMock = nullptr;
    HostImplMock* p_hostImplMock = nullptr;
    AudioOutputPortMock* p_audioOutputPortMock = nullptr;
    VideoResolutionMock* p_videoResolutionMock = nullptr;
    VideoOutputPortMock* p_videoOutputPortMock = nullptr;
    VideoOutputPortConfigImplMock* p_videoOutputPortConfigImplMock = nullptr;
    VideoOutputPortTypeMock* p_videoOutputPortTypeMock = nullptr;
    RfcApiImplMock* p_rfcApiImplMock = nullptr;
    NiceMock<ServiceMock> service;
    NiceMock<COMLinkMock> comLinkMock;
    WrapsImplMock* p_wrapsImplMock = nullptr;
    Core::Sink<NiceMock<SystemInfo>> subSystem;

    DeviceInfoTest()
        : plugin(Core::ProxyType<Plugin::DeviceInfo>::Create())
        , handler(*plugin)
        , INIT_CONX(1, 0)
    {
        p_iarmBusImplMock = new NiceMock<IarmBusImplMock>;
        IarmBus::setImpl(p_iarmBusImplMock);

        p_managerImplMock = new NiceMock<ManagerImplMock>;
        device::Manager::setImpl(p_managerImplMock);

        p_hostImplMock = new NiceMock<HostImplMock>;
        device::Host::setImpl(p_hostImplMock);

        p_audioOutputPortMock = new NiceMock<AudioOutputPortMock>;
        device::AudioOutputPort::setImpl(p_audioOutputPortMock);

        p_videoResolutionMock = new NiceMock<VideoResolutionMock>;
        device::VideoResolution::setImpl(p_videoResolutionMock);

        p_videoOutputPortMock = new NiceMock<VideoOutputPortMock>;
        device::VideoOutputPort::setImpl(p_videoOutputPortMock);

        p_videoOutputPortConfigImplMock = new NiceMock<VideoOutputPortConfigImplMock>;
        device::VideoOutputPortConfig::setImpl(p_videoOutputPortConfigImplMock);

        p_videoOutputPortTypeMock = new NiceMock<VideoOutputPortTypeMock>;
        device::VideoOutputPortType::setImpl(p_videoOutputPortTypeMock);

        p_rfcApiImplMock = new NiceMock<RfcApiImplMock>;
        RfcApi::setImpl(p_rfcApiImplMock);

        p_wrapsImplMock = new NiceMock<WrapsImplMock>;
        Wraps::setImpl(p_wrapsImplMock);

        // Create implementation objects
        deviceInfoImplementation = Core::ProxyType<Plugin::DeviceInfoImplementation>::Create();
        deviceAudioCapabilities = Core::ProxyType<Plugin::DeviceAudioCapabilities>::Create();
        deviceVideoCapabilities = Core::ProxyType<Plugin::DeviceVideoCapabilities>::Create();

        ON_CALL(service, ConfigLine())
            .WillByDefault(Return("{\"root\":{\"mode\":\"Off\"}}"));
        ON_CALL(service, WebPrefix())
            .WillByDefault(Return(webPrefix));
        ON_CALL(service, SubSystems())
            .WillByDefault(Invoke(
                [&]() {
                    PluginHost::ISubSystem* result = (&subSystem);
                    result->AddRef();
                    return result;
                }));
        ON_CALL(service, COMLink())
            .WillByDefault(Return(&comLinkMock));

#ifdef USE_THUNDER_R4
        ON_CALL(comLinkMock, Instantiate(::testing::_, ::testing::_, ::testing::_))
            .WillByDefault(::testing::Invoke(
                [&](const RPC::Object& object, const uint32_t waitTime, uint32_t& connectionId) -> void* {
                    if (object.ClassName() == _T("DeviceInfoImplementation")) {
                        return &deviceInfoImplementation;
                    } else if (object.ClassName() == _T("DeviceAudioCapabilities")) {
                        return &deviceAudioCapabilities;
                    } else if (object.ClassName() == _T("DeviceVideoCapabilities")) {
                        return &deviceVideoCapabilities;
                    }
                    return nullptr;
                }));
#else
        ON_CALL(comLinkMock, Instantiate(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
            .WillByDefault(::testing::Invoke(
                [&](const uint32_t waitTime, const string& className, const uint32_t interfaceId, const uint32_t version, uint32_t& connectionId) -> void* {
                    if (className == _T("DeviceInfoImplementation")) {
                        return &deviceInfoImplementation;
                    } else if (className == _T("DeviceAudioCapabilities")) {
                        return &deviceAudioCapabilities;
                    } else if (className == _T("DeviceVideoCapabilities")) {
                        return &deviceVideoCapabilities;
                    }
                    return nullptr;
                }));
#endif

        EXPECT_EQ(string(""), plugin->Initialize(&service));

        if (0 != system("mkdir -p /opt/www/authService")){ /* do nothig */
        }
    }

    virtual ~DeviceInfoTest()
    {
        plugin->Deinitialize(&service);

        RfcApi::setImpl(nullptr);
        if (p_rfcApiImplMock != nullptr) {
            delete p_rfcApiImplMock;
            p_rfcApiImplMock = nullptr;
        }

        Wraps::setImpl(nullptr);
        if (p_wrapsImplMock != nullptr) {
            delete p_wrapsImplMock;
            p_wrapsImplMock = nullptr;
        }

        device::VideoOutputPortType::setImpl(nullptr);
        if (p_videoOutputPortTypeMock != nullptr) {
            delete p_videoOutputPortTypeMock;
            p_videoOutputPortTypeMock = nullptr;
        }

        device::VideoOutputPortConfig::setImpl(nullptr);
        if (p_videoOutputPortConfigImplMock != nullptr) {
            delete p_videoOutputPortConfigImplMock;
            p_videoOutputPortConfigImplMock = nullptr;
        }

        device::VideoOutputPort::setImpl(nullptr);
        if (p_videoOutputPortMock != nullptr) {
            delete p_videoOutputPortMock;
            p_videoOutputPortMock = nullptr;
        }

        device::VideoResolution::setImpl(nullptr);
        if (p_videoResolutionMock != nullptr) {
            delete p_videoResolutionMock;
            p_videoResolutionMock = nullptr;
        }

        device::AudioOutputPort::setImpl(nullptr);
        if (p_audioOutputPortMock != nullptr) {
            delete p_audioOutputPortMock;
            p_audioOutputPortMock = nullptr;
        }

        device::Host::setImpl(nullptr);
        if (p_hostImplMock != nullptr) {
            delete p_hostImplMock;
            p_hostImplMock = nullptr;
        }

        device::Manager::setImpl(nullptr);
        if (p_managerImplMock != nullptr) {
            delete p_managerImplMock;
            p_managerImplMock = nullptr;
        }

        IarmBus::setImpl(nullptr);
        if (p_iarmBusImplMock != nullptr) {
            delete p_iarmBusImplMock;
            p_iarmBusImplMock = nullptr;
        }
    }
};

TEST_F(DeviceInfoTest, SerialNumber_Success_FromMFR)
{
    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(::testing::Invoke(
            [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                if (methodName && strcmp(methodName, IARM_BUS_MFRLIB_API_GetSerializedData) == 0) {
                    auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                    if (param->type == mfrSERIALIZED_TYPE_SERIALNUMBER) {
                        strncpy(param->buffer, "TEST12345", sizeof(param->buffer) - 1);
                        param->buffer[sizeof(param->buffer) - 1] = '\0';
                        param->bufLen = strlen(param->buffer);
                        return IARM_RESULT_SUCCESS;
                    }
                }
                return IARM_RESULT_INVALID_PARAM;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("serialnumber"), _T(""), response));
    EXPECT_EQ(response, _T("{\"serialnumber\":\"TEST12345\"}"));
}

TEST_F(DeviceInfoTest, SerialNumber_Success_FromRFC)
{
    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(Return(IARM_RESULT_INVALID_PARAM));

    EXPECT_CALL(*p_rfcApiImplMock, getRFCParameter(_, _, _))
        .WillRepeatedly(Invoke(
            [](char* pcCallerID, const char* pcParameterName, RFC_ParamData_t* pstParamData) {
                if (string(pcParameterName) == string("Device.DeviceInfo.SerialNumber")) {
                    strncpy(pstParamData->value, "RFC12345", sizeof(pstParamData->value));
                    return WDMP_SUCCESS;
                }
                return WDMP_FAILURE;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("serialnumber"), _T(""), response));
    EXPECT_EQ(response, _T("{\"serialnumber\":\"RFC12345\"}"));
}

TEST_F(DeviceInfoTest, SerialNumber_Failure_BothSourcesFail)
{
    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(Return(IARM_RESULT_INVALID_PARAM));

    EXPECT_CALL(*p_rfcApiImplMock, getRFCParameter(_, _, _))
        .WillRepeatedly(Return(WDMP_FAILURE));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("serialnumber"), _T(""), response));
}

TEST_F(DeviceInfoTest, Sku_Success_FromFile)
{
    std::ofstream file("/etc/device.properties");
    file << "MODEL_NUM=SKU-TEST-001\n";
    file.close();

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("modelid"), _T(""), response));
    EXPECT_EQ(response, _T("{\"sku\":\"SKU-TEST-001\"}"));
}

TEST_F(DeviceInfoTest, Make_Success_FromFile)
{

    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(Return(IARM_RESULT_INVALID_PARAM));

    std::ofstream file("/etc/device.properties");
    file << "MFG_NAME=FileManufacturer\n";
    file.close();

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("make"), _T(""), response));
    EXPECT_EQ(response, _T("{\"make\":\"FileManufacturer\"}"));
}

TEST_F(DeviceInfoTest, SocName_Success)
{

    std::ofstream file("/etc/device.properties");
    file << "SOC=BCM7218\n";
    file.close();

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("socname"), _T(""), response));
    EXPECT_EQ(response, _T("{\"socname\":\"BCM7218\"}"));
}

TEST_F(DeviceInfoTest, DeviceType_Success_IpTv)
{
    std::ofstream file("/etc/authService.conf");
    file << "deviceType=IpTv\n";
    file.close();

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("devicetype"), _T(""), response));
    EXPECT_EQ(response, _T("{\"devicetype\":\"IpTv\"}"));
}

TEST_F(DeviceInfoTest, DeviceType_Success_IpStb)
{
    std::ofstream file("/etc/authService.conf");
    file << "deviceType=IpStb\n";
    file.close();

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("devicetype"), _T(""), response));
    EXPECT_EQ(response, _T("{\"devicetype\":\"IpStb\"}"));

}

TEST_F(DeviceInfoTest, DeviceType_Success_QamIpStb)
{
    std::ofstream file("/etc/authService.conf");
    file << "deviceType=QamIpStb\n";
    file.close();

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("devicetype"), _T(""), response));
    EXPECT_EQ(response, _T("{\"devicetype\":\"QamIpStb\"}"));
}

TEST_F(DeviceInfoTest, DeviceType_Success_FromDeviceProperties_MediaClient)
{
    removeFile("/etc/authService.conf");

    std::ofstream file("/etc/device.properties");
    file << "DEVICE_TYPE=mediaclient\n";
    file.close();

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("devicetype"), _T(""), response));
    EXPECT_EQ(response, _T("{\"devicetype\":\"IpStb\"}"));

}

TEST_F(DeviceInfoTest, DeviceType_Success_FromDeviceProperties_Hybrid)
{
    removeFile("/etc/authService.conf");

    std::ofstream file("/etc/device.properties");
    file << "DEVICE_TYPE=hybrid\n";
    file.close();

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("devicetype"), _T(""), response));
    EXPECT_EQ(response, _T("{\"devicetype\":\"QamIpStb\"}"));
}

TEST_F(DeviceInfoTest, DeviceType_Success_FromDeviceProperties_Other)
{
    removeFile("/etc/authService.conf");

    std::ofstream file("/etc/device.properties");
    file << "DEVICE_TYPE=other\n";
    file.close();

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("devicetype"), _T(""), response));
    EXPECT_EQ(response, _T("{\"devicetype\":\"IpTv\"}"));
}


TEST_F(DeviceInfoTest, Model_Success_FromFile)
{

    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(Return(IARM_RESULT_INVALID_PARAM));

    std::ofstream file("/etc/device.properties");
    file << "FRIENDLY_ID=TestModel123\n";
    file.close();

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("modelname"), _T(""), response));
    EXPECT_EQ(response, _T("{\"model\":\"TestModel123\"}"));
}

TEST_F(DeviceInfoTest, ReleaseVersion_Success)
{
    std::ofstream file("/version.txt");
    file << "imagename:CUSTOM_VBN_22.03s_sprint_20220331225312sdy_NG\n";
    file.close();

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("releaseversion"), _T(""), response));
    EXPECT_EQ(response, _T("{\"releaseversion\":\"22.03.0.0\"}"));
}

TEST_F(DeviceInfoTest, ChipSet_Success)
{
    std::ofstream file("/etc/device.properties");
    file << "CHIPSET_NAME=BCM7252S\n";
    file.close();

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("chipset"), _T(""), response));
    EXPECT_EQ(response, _T("{\"chipset\":\"BCM7252S\"}"));
}

TEST_F(DeviceInfoTest, FirmwareVersion_Success)
{
    std::ofstream file("/version.txt");
    file << "imagename:TEST_IMAGE_V1\n";
    file << "SDK_VERSION=18.4\n";
    file << "MEDIARITE=9.0.1\n";
    file << "YOCTO_VERSION=dunfell\n";
    file.close();

    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(::testing::Invoke(
            [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                if (methodName && strcmp(methodName, IARM_BUS_MFRLIB_API_GetSerializedData) == 0) {
                    auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                    if (param->type == mfrSERIALIZED_TYPE_PDRIVERSION) {
                        strncpy(param->buffer, "PDRI_1.2.3", sizeof(param->buffer) - 1);
                        param->buffer[sizeof(param->buffer) - 1] = '\0';
                        param->bufLen = strlen(param->buffer);
                        return IARM_RESULT_SUCCESS;
                    }
                }
                return IARM_RESULT_INVALID_PARAM;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("firmwareversion"), _T(""), response));
    EXPECT_EQ(response, _T("{\"imagename\":\"TEST_IMAGE_V1\",\"sdk\":\"18.4\",\"mediarite\":\"9.0.1\",\"yocto\":\"dunfell\",\"pdri\":\"PDRI_1.2.3\"}"));
}

TEST_F(DeviceInfoTest, Sku_Success_FromMFR)
{
    // Ensure file doesn't exist before calling implementation
    removeFile("/etc/device.properties");

    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(Invoke(
            [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                if (string(ownerName) == string(_T(IARM_BUS_MFRLIB_NAME)) &&
                    string(methodName) == string(_T(IARM_BUS_MFRLIB_API_GetSerializedData))) {
                    auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                    if (param->type == mfrSERIALIZED_TYPE_MODELNAME) {
                        param->bufLen = strlen("MFR-SKU-002");
                        strncpy(param->buffer, "MFR-SKU-002", sizeof(param->buffer));
                        return IARM_RESULT_SUCCESS;
                    }
                }
                return IARM_RESULT_INVALID_PARAM;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("modelid"), _T(""), response));
    EXPECT_EQ(response, _T("{\"sku\":\"MFR-SKU-002\"}"));
}

TEST_F(DeviceInfoTest, Sku_Success_FromRFC)
{
    // Ensure file doesn't exist before calling implementation
    removeFile("/etc/device.properties");

    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(Return(IARM_RESULT_INVALID_PARAM));

    EXPECT_CALL(*p_rfcApiImplMock, getRFCParameter(_, _, _))
        .WillRepeatedly(Invoke(
            [](char* pcCallerID, const char* pcParameterName, RFC_ParamData_t* pstParamData) {
                if (string(pcParameterName) == string("Device.DeviceInfo.ModelName")) {
                    strncpy(pstParamData->value, "RFC-SKU-003", sizeof(pstParamData->value));
                    return WDMP_SUCCESS;
                }
                return WDMP_FAILURE;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("modelid"), _T(""), response));
    EXPECT_EQ(response, _T("{\"sku\":\"RFC-SKU-003\"}"));
}

TEST_F(DeviceInfoTest, Sku_Failure_AllSourcesFail)
{
    removeFile("/etc/device.properties");

    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(Return(IARM_RESULT_INVALID_PARAM));

    EXPECT_CALL(*p_rfcApiImplMock, getRFCParameter(_, _, _))
        .WillRepeatedly(Return(WDMP_FAILURE));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("modelid"), _T(""), response));
}

TEST_F(DeviceInfoTest, Make_Success_FromMFR)
{
    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(::testing::Invoke(
            [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                if (methodName && strcmp(methodName, IARM_BUS_MFRLIB_API_GetSerializedData) == 0) {
                    auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                    if (param->type == mfrSERIALIZED_TYPE_MANUFACTURER) {
                        strncpy(param->buffer, "TestManufacturer", sizeof(param->buffer) - 1);
                        param->buffer[sizeof(param->buffer) - 1] = '\0';
                        param->bufLen = strlen(param->buffer);
                        return IARM_RESULT_SUCCESS;
                    }
                }
                return IARM_RESULT_INVALID_PARAM;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("make"), _T(""), response));
    EXPECT_EQ(response, _T("{\"make\":\"TestManufacturer\"}"));
}

TEST_F(DeviceInfoTest, Make_Failure_BothSourcesFail)
{
    removeFile("/etc/device.properties");

    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(Return(IARM_RESULT_INVALID_PARAM));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("make"), _T(""), response));
}

TEST_F(DeviceInfoTest, Model_Failure_FileNotFound)
{
    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(Return(IARM_RESULT_INVALID_PARAM));

    removeFile("/etc/device.properties");

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("modelname"), _T(""), response));
}

TEST_F(DeviceInfoTest, DeviceType_Failure_BothFilesNotFound)
{
    removeFile("/etc/authService.conf");
    removeFile("/etc/device.properties");

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("devicetype"), _T(""), response));
}

TEST_F(DeviceInfoTest, SocName_Failure_FileNotFound)
{
    removeFile("/etc/device.properties");

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("socname"), _T(""), response));
}

TEST_F(DeviceInfoTest, DistributorId_Success_FromFile)
{
    removeFile("/opt/www/authService/partnerId3.dat");

    std::ofstream file("/opt/www/authService/partnerId3.dat");
    file << "PARTNER123\n";
    file.close();

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("distributorid"), _T(""), response));
    EXPECT_EQ(response, _T("{\"distributorid\":\"PARTNER123\"}"));
}

TEST_F(DeviceInfoTest, DistributorId_Success_FromRFC)
{
    removeFile("/opt/www/authService/partnerId3.dat");

    EXPECT_CALL(*p_rfcApiImplMock, getRFCParameter(_, ::testing::StrEq("Device.DeviceInfo.X_RDKCENTRAL-COM_Syndication.PartnerId"), _))
        .WillRepeatedly(::testing::Invoke(
            [](char* pcCallerID, const char* pcParameterName, RFC_ParamData_t* pstParamData) {
                strncpy(pstParamData->value, "RFCPARTNER456", sizeof(pstParamData->value) - 1);
                pstParamData->value[sizeof(pstParamData->value) - 1] = '\0';
                pstParamData->type = WDMP_STRING;
                return WDMP_SUCCESS;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("distributorid"), _T(""), response));
    EXPECT_EQ(response, _T("{\"distributorid\":\"RFCPARTNER456\"}"));
}

TEST_F(DeviceInfoTest, DistributorId_Failure_BothSourcesFail)
{
    removeFile("/opt/www/authService/partnerId3.dat");

    EXPECT_CALL(*p_rfcApiImplMock, getRFCParameter(_, _, _))
        .WillRepeatedly(Return(WDMP_FAILURE));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("distributorid"), _T(""), response));
}

TEST_F(DeviceInfoTest, Brand_Success_FromTmpFile)
{
    std::ofstream file("/tmp/.manufacturer");
    file << "TestBrand\n";
    file.close();

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("brandname"), _T(""), response));
    EXPECT_EQ(response, _T("{\"brand\":\"TestBrand\"}"));
}

TEST_F(DeviceInfoTest, Brand_Success_FromMFR)
{
    removeFile("/tmp/.manufacturer");

    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(::testing::Invoke(
            [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                if (methodName && strcmp(methodName, IARM_BUS_MFRLIB_API_GetSerializedData) == 0) {
                    auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                    if (param->type == mfrSERIALIZED_TYPE_MANUFACTURER) {
                        strncpy(param->buffer, "MFRBrand", sizeof(param->buffer) - 1);
                        param->buffer[sizeof(param->buffer) - 1] = '\0';
                        param->bufLen = strlen(param->buffer);
                        return IARM_RESULT_SUCCESS;
                    }
                }
                return IARM_RESULT_INVALID_PARAM;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("brandname"), _T(""), response));
    EXPECT_EQ(response, _T("{\"brand\":\"MFRBrand\"}"));
}

TEST_F(DeviceInfoTest, Brand_Failure_BothSourcesFail)
{
    removeFile("/tmp/.manufacturer");

    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(Return(IARM_RESULT_INVALID_PARAM));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("brandname"), _T(""), response));
}

TEST_F(DeviceInfoTest, ReleaseVersion_DefaultVersion_InvalidPattern)
{
    removeFile("/version.txt");

    std::ofstream file("/version.txt");
    file << "releaseversion:99.99.0.0\n";
    file.close();

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("releaseversion"), _T(""), response));
    EXPECT_EQ(response, _T("{\"releaseversion\":\"99.99.0.0\"}"));

}

TEST_F(DeviceInfoTest, ReleaseVersion_DefaultVersion_FileNotFound)
{
    removeFile("/version.txt");

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("releaseversion"), _T(""), response));
    EXPECT_EQ(response, _T("{\"releaseversion\":\"99.99.0.0\"}"));
}

TEST_F(DeviceInfoTest, ChipSet_Failure_FileNotFound)
{
    removeFile("/etc/device.properties");

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("chipset"), _T(""), response));
}

TEST_F(DeviceInfoTest, FirmwareVersion_Success_MissingOptionalFields)
{
    std::ofstream file("/version.txt");
    file << "imagename:TEST_IMAGE_V2\n";
    file.close();

    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(Return(IARM_RESULT_INVALID_PARAM));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("firmwareversion"), _T(""), response));
    EXPECT_EQ(response, _T("{\"imagename\":\"TEST_IMAGE_V2\",\"sdk\":\"\",\"mediarite\":\"\",\"yocto\":\"\",\"pdri\":\"\"}"));
}

TEST_F(DeviceInfoTest, FirmwareVersion_Failure_ImageNameNotFound)
{
    removeFile("/version.txt");

    std::ofstream file("/version.txt");
    file << "SDK_VERSION=18.4\n";
    file.close();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("firmwareversion"), _T(""), response));
}

TEST_F(DeviceInfoTest, FirmwareVersion_Failure_FileNotFound)
{
    removeFile("/version.txt");

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("firmwareversion"), _T(""), response));
}

TEST_F(DeviceInfoTest, DISABLE_SystemInfo_Success)
{
    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(::testing::Invoke(
            [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                if (methodName && strcmp(methodName, IARM_BUS_MFRLIB_API_GetSerializedData) == 0) {
                    auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                    if (param->type == mfrSERIALIZED_TYPE_SERIALNUMBER) {
                        strncpy(param->buffer, "SYSTEMSERIAL123", sizeof(param->buffer) - 1);
                        param->buffer[sizeof(param->buffer) - 1] = '\0';
                        param->bufLen = strlen(param->buffer);
                        return IARM_RESULT_SUCCESS;
                    }
                }
                return IARM_RESULT_INVALID_PARAM;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("systeminfo"), _T(""), response));
    EXPECT_TRUE(response.find("\"version\":") != string::npos);
    EXPECT_TRUE(response.find("\"uptime\":") != string::npos);
    EXPECT_TRUE(response.find("\"totalram\":") != string::npos);
    EXPECT_TRUE(response.find("\"freeram\":") != string::npos);
    EXPECT_TRUE(response.find("\"devicename\":") != string::npos);
    EXPECT_TRUE(response.find("\"cpuload\":") != string::npos);
    EXPECT_TRUE(response.find("\"serialnumber\":\"SYSTEMSERIAL123\"") != string::npos);
    EXPECT_TRUE(response.find("\"time\":") != string::npos);
}

TEST_F(DeviceInfoTest, Addresses_Success)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("addresses"), _T(""), response));
    EXPECT_TRUE(response.find("\"name\":") != string::npos);
    EXPECT_TRUE(response.find("\"mac\":") != string::npos);
}

TEST_F(DeviceInfoTest, SupportedAudioPorts_Success)
{
    device::List<device::AudioOutputPort> audioPorts;
    device::AudioOutputPort port1, port2;
    static const string portName1 = "HDMI0";
    static const string portName2 = "SPDIF";

    EXPECT_CALL(*p_audioOutputPortMock, getName())
        .WillOnce(ReturnRef(portName1))
        .WillOnce(ReturnRef(portName2));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPorts())
        .WillOnce(Invoke([&]() {
            audioPorts.push_back(port1);
            audioPorts.push_back(port2);
            return audioPorts;
        }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedaudioports"), _T(""), response));
    EXPECT_TRUE(response.find("\"supportedAudioPorts\":[") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceInfoTest, SupportedAudioPorts_Exception_DeviceException)
{
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPorts())
        .WillOnce(Invoke([]() -> device::List<device::AudioOutputPort> {
            throw device::Exception("Test exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedaudioports"), _T(""), response));
}

TEST_F(DeviceInfoTest, SupportedAudioPorts_Exception_StdException)
{
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPorts())
        .WillOnce(Invoke([]() -> device::List<device::AudioOutputPort> {
            throw std::runtime_error("Test exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedaudioports"), _T(""), response));
}

TEST_F(DeviceInfoTest, SupportedAudioPorts_Exception_UnknownException)
{
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPorts())
        .WillOnce(Invoke([]() -> device::List<device::AudioOutputPort> {
            throw 42;
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedaudioports"), _T(""), response));
}

// =========== Additional Negative Tests ===========

TEST_F(DeviceInfoTest, SerialNumber_Negative_EmptyBuffer)
{
    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillOnce(Invoke(
            [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                param->bufLen = 0;
                param->buffer[0] = '\0';
                return IARM_RESULT_SUCCESS;
            }));

    EXPECT_CALL(*p_rfcApiImplMock, getRFCParameter(_, _, _))
        .WillOnce(Return(WDMP_FAILURE));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("serialnumber"), _T(""), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceInfoTest, Sku_Negative_InvalidFileFormat)
{
    std::ofstream file("/etc/device.properties");
    file << "INVALID_FORMAT_LINE\n";
    file << "MODEL_NUM_WITHOUT_EQUAL_SIGN\n";
    file.close();

    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillOnce(Return(IARM_RESULT_INVALID_PARAM));

    EXPECT_CALL(*p_rfcApiImplMock, getRFCParameter(_, _, _))
        .WillOnce(Return(WDMP_FAILURE));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("modelid"), _T(""), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceInfoTest, Make_Negative_InvalidFileFormat)
{
    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillOnce(Return(IARM_RESULT_INVALID_PARAM));

    std::ofstream file("/etc/device.properties");
    file << "MFG_NAME_NO_VALUE=\n";
    file << "OTHER_KEY=value\n";
    file.close();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("make"), _T(""), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceInfoTest, Model_Negative_EmptyFriendlyId)
{
    std::ofstream file("/etc/device.properties");
    file << "FRIENDLY_ID=\n";
    file.close();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("modelname"), _T(""), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceInfoTest, Model_Negative_FileAccessException)
{
    removeFile("/etc/device.properties");

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("modelname"), _T(""), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceInfoTest, DeviceType_Negative_MalformedFile)
{
    removeFile("/etc/device.properties");

    std::ofstream file("/etc/authService.conf");
    file << "INVALID LINE FORMAT\n";
    file << "NO EQUAL SIGN\n";
    file.close();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("devicetype"), _T(""), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceInfoTest, SocName_Negative_EmptySocValue)
{
    std::ofstream file("/etc/device.properties");
    file << "SOC=\n";
    file.close();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("socname"), _T(""), response));
    EXPECT_TRUE(response.empty());

}

TEST_F(DeviceInfoTest, SocName_Negative_MalformedFile)
{
    std::ofstream file("/etc/device.properties");
    file << "SOC_WITHOUT_VALUE\n";
    file << "OTHER_KEY=value\n";
    file.close();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("socname"), _T(""), response));
    EXPECT_TRUE(response.empty());

}

TEST_F(DeviceInfoTest, DistributorId_Negative_EmptyFile)
{
    std::ofstream file("/opt/www/authService/partnerId3.dat");
    file << "";
    file.close();

    EXPECT_CALL(*p_rfcApiImplMock, getRFCParameter(_, _, _))
        .WillOnce(Return(WDMP_FAILURE));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("distributorid"), _T(""), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceInfoTest, Brand_Negative_BothSourcesEmpty)
{
    std::ofstream file("/tmp/.manufacturer");
    file << "";
    file.close();

    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillOnce(Return(IARM_RESULT_INVALID_PARAM));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("brandname"), _T(""), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceInfoTest, ReleaseVersion_Negative_MalformedImageName)
{
    std::ofstream file("/version.txt");
    file << "imagename:\n";
    file.close();

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("releaseversion"), _T(""), response));
    EXPECT_EQ(response, _T("{\"releaseversion\":\"99.99.0.0\"}"));

}

TEST_F(DeviceInfoTest, ReleaseVersion_Negative_SpecialCharacters)
{
    std::ofstream file("/version.txt");
    file << "imagename:ABC@#$%XYZ\n";
    file.close();

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("releaseversion"), _T(""), response));
    EXPECT_EQ(response, _T("{\"releaseversion\":\"99.99.0.0\"}"));

}

TEST_F(DeviceInfoTest, ChipSet_Negative_EmptyChipsetValue)
{
    std::ofstream file("/etc/device.properties");
    file << "CHIPSET_NAME=\n";
    file.close();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("chipset"), _T(""), response));
    EXPECT_TRUE(response.empty());

}

TEST_F(DeviceInfoTest, ChipSet_Negative_MalformedFile)
{
    std::ofstream file("/etc/device.properties");
    file << "CHIPSET_NAME_NO_VALUE\n";
    file << "RANDOM_DATA\n";
    file.close();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("chipset"), _T(""), response));
    EXPECT_TRUE(response.empty());

}

TEST_F(DeviceInfoTest, FirmwareVersion_Negative_EmptyImageName)
{
    std::ofstream file("/version.txt");
    file << "imagename:\n";
    file << "SDK_VERSION=18.4\n";
    file.close();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("firmwareversion"), _T(""), response));
    EXPECT_TRUE(response.empty());

}

TEST_F(DeviceInfoTest, FirmwareVersion_Negative_MalformedVersionFile)
{
    std::ofstream file("/version.txt");
    file << "INVALID_FORMAT\n";
    file << "NO_IMAGENAME_KEY\n";
    file.close();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("firmwareversion"), _T(""), response));
    EXPECT_TRUE(response.empty());

}

TEST_F(DeviceInfoTest, SupportedAudioPorts_Negative_GetNameThrowsException)
{
    device::List<device::AudioOutputPort> audioPorts;
    device::AudioOutputPort port1;

    EXPECT_CALL(*p_audioOutputPortMock, getName())
        .WillOnce(Invoke([]() -> const string& {
            throw device::Exception("getName exception");
            static const string portName = "HDMI0";
            return portName;
        }));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPorts())
        .WillOnce(Invoke([&]() {
            audioPorts.push_back(port1);
            return audioPorts;
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedaudioports"), _T(""), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceInfoTest, SupportedAudioPorts_Negative_EmptyPortList)
{
    device::List<device::AudioOutputPort> audioPorts;

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPorts())
        .WillOnce(Return(audioPorts));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedaudioports"), _T(""), response));
    EXPECT_FALSE(response.find("\"supportedAudioPorts\":[]") != string::npos);
}

// =========== Additional Comprehensive Positive Tests ===========

TEST_F(DeviceInfoTest, SerialNumber_Positive_LongSerialNumber)
{
    std::string longSerial(255, 'X');

    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillOnce(Invoke(
            [&longSerial](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                param->bufLen = longSerial.length();
                strncpy(param->buffer, longSerial.c_str(), sizeof(param->buffer));
                return IARM_RESULT_SUCCESS;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("serialnumber"), _T(""), response));
    EXPECT_TRUE(response.find(longSerial) != string::npos);
}

TEST_F(DeviceInfoTest, SerialNumber_Positive_SpecialCharacters)
{
    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(::testing::Invoke(
            [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                if (methodName && strcmp(methodName, IARM_BUS_MFRLIB_API_GetSerializedData) == 0) {
                    auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                    if (param->type == mfrSERIALIZED_TYPE_SERIALNUMBER) {
                        strncpy(param->buffer, "SN-123_ABC.DEF#456", sizeof(param->buffer) - 1);
                        param->buffer[sizeof(param->buffer) - 1] = '\0';
                        param->bufLen = strlen(param->buffer);
                        return IARM_RESULT_SUCCESS;
                    }
                }
                return IARM_RESULT_INVALID_PARAM;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("serialnumber"), _T(""), response));
    EXPECT_EQ(response, _T("{\"serialnumber\":\"SN-123_ABC.DEF#456\"}"));
}

TEST_F(DeviceInfoTest, SerialNumber_Positive_NumericOnly)
{
    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(::testing::Invoke(
            [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                if (methodName && strcmp(methodName, IARM_BUS_MFRLIB_API_GetSerializedData) == 0) {
                    auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                    if (param->type == mfrSERIALIZED_TYPE_SERIALNUMBER) {
                        strncpy(param->buffer, "1234567890", sizeof(param->buffer) - 1);
                        param->buffer[sizeof(param->buffer) - 1] = '\0';
                        param->bufLen = strlen(param->buffer);
                        return IARM_RESULT_SUCCESS;
                    }
                }
                return IARM_RESULT_INVALID_PARAM;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("serialnumber"), _T(""), response));
    EXPECT_EQ(response, _T("{\"serialnumber\":\"1234567890\"}"));
}

TEST_F(DeviceInfoTest, Make_Positive_LongName)
{
    std::string longName(200, 'M');

    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillOnce(Invoke(
            [&longName](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                param->bufLen = longName.length();
                strncpy(param->buffer, longName.c_str(), sizeof(param->buffer));
                return IARM_RESULT_SUCCESS;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("make"), _T(""), response));
    EXPECT_TRUE(response.find(longName) != string::npos);
}

TEST_F(DeviceInfoTest, DistributorId_Positive_RFCWithSpecialChars)
{
    removeFile("/opt/www/authService/partnerId3.dat");

    EXPECT_CALL(*p_rfcApiImplMock, getRFCParameter(_, ::testing::StrEq("Device.DeviceInfo.X_RDKCENTRAL-COM_Syndication.PartnerId"), _))
        .WillRepeatedly(::testing::Invoke(
            [](char* pcCallerID, const char* pcParameterName, RFC_ParamData_t* pstParamData) {
                strncpy(pstParamData->value, "RFC_DIST_ID.001", sizeof(pstParamData->value) - 1);
                pstParamData->value[sizeof(pstParamData->value) - 1] = '\0';
                pstParamData->type = WDMP_STRING;
                return WDMP_SUCCESS;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("distributorid"), _T(""), response));
    EXPECT_EQ(response, _T("{\"distributorid\":\"RFC_DIST_ID.001\"}"));
}

TEST_F(DeviceInfoTest, Brand_Positive_FileWithWhitespace)
{
    std::ofstream file("/tmp/.manufacturer");
    file << "  BrandName  \n";
    file.close();

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("brandname"), _T(""), response));
    EXPECT_TRUE(response.find("BrandName") != string::npos);
}

TEST_F(DeviceInfoTest, Brand_Positive_MFRFallback)
{
    removeFile("/tmp/.manufacturer");

    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(::testing::Invoke(
            [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                if (methodName && strcmp(methodName, IARM_BUS_MFRLIB_API_GetSerializedData) == 0) {
                    auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                    if (param->type == mfrSERIALIZED_TYPE_MANUFACTURER) {
                        strncpy(param->buffer, "FallbackBrand", sizeof(param->buffer) - 1);
                        param->buffer[sizeof(param->buffer) - 1] = '\0';
                        param->bufLen = strlen(param->buffer);
                        return IARM_RESULT_SUCCESS;
                    }
                }
                return IARM_RESULT_INVALID_PARAM;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("brandname"), _T(""), response));
    EXPECT_EQ(response, _T("{\"brand\":\"FallbackBrand\"}"));
}

TEST_F(DeviceInfoTest, DISABLE_SystemInfo_Positive_AllFieldsPresent)
{
    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(::testing::Invoke(
            [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                if (methodName && strcmp(methodName, IARM_BUS_MFRLIB_API_GetSerializedData) == 0) {
                    auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                    if (param->type == mfrSERIALIZED_TYPE_SERIALNUMBER) {
                        strncpy(param->buffer, "SYSSERIAL999", sizeof(param->buffer) - 1);
                        param->buffer[sizeof(param->buffer) - 1] = '\0';
                        param->bufLen = strlen(param->buffer);
                        return IARM_RESULT_SUCCESS;
                    }
                }
                return IARM_RESULT_INVALID_PARAM;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("systeminfo"), _T(""), response));
    EXPECT_TRUE(response.find("\"version\":") != string::npos);
    EXPECT_TRUE(response.find("\"uptime\":") != string::npos);
    EXPECT_TRUE(response.find("\"totalram\":") != string::npos);
    EXPECT_TRUE(response.find("\"freeram\":") != string::npos);
    EXPECT_TRUE(response.find("\"devicename\":") != string::npos);
    EXPECT_TRUE(response.find("\"cpuload\":") != string::npos);
    EXPECT_TRUE(response.find("\"serialnumber\":\"SYSSERIAL999\"") != string::npos);
    EXPECT_TRUE(response.find("\"time\":") != string::npos);
}

TEST_F(DeviceInfoTest, Addresses_Positive_HasRequiredFields)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("addresses"), _T(""), response));
    EXPECT_TRUE(response.find("\"name\":") != string::npos);
    EXPECT_TRUE(response.find("\"mac\":") != string::npos);
}

TEST_F(DeviceInfoTest, SupportedAudioPorts_Positive_MultiplePortTypes)
{
    device::List<device::AudioOutputPort> audioPorts;
    device::AudioOutputPort port1, port2, port3;
    static const string portName1 = "HDMI0";
    static const string portName2 = "SPDIF";
    static const string portName3 = "SPEAKER";

    EXPECT_CALL(*p_audioOutputPortMock, getName())
        .WillOnce(ReturnRef(portName1))
        .WillOnce(ReturnRef(portName2))
        .WillOnce(ReturnRef(portName3));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPorts())
        .WillOnce(Invoke([&]() {
            audioPorts.push_back(port1);
            audioPorts.push_back(port2);
            audioPorts.push_back(port3);
            return audioPorts;
        }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedaudioports"), _T(""), response));
    EXPECT_TRUE(response.find("\"HDMI0\"") != string::npos);
    EXPECT_TRUE(response.find("\"SPDIF\"") != string::npos);
    EXPECT_TRUE(response.find("\"SPEAKER\"") != string::npos);
}

TEST_F(DeviceInfoTest, SupportedAudioPorts_Positive_SinglePort)
{
    device::List<device::AudioOutputPort> audioPorts;
    device::AudioOutputPort port1;
    static const string portName = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getName())
        .WillOnce(ReturnRef(portName));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPorts())
        .WillOnce(Invoke([&]() {
            audioPorts.push_back(port1);
            return audioPorts;
        }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedaudioports"), _T(""), response));
    EXPECT_TRUE(response.find("\"supportedAudioPorts\":[") != string::npos);
    EXPECT_TRUE(response.find("\"HDMI0\"") != string::npos);
}

// =========== Boundary and Edge Case Tests ===========

TEST_F(DeviceInfoTest, Boundary_SerialNumber_MinLength)
{
    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(::testing::Invoke(
            [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                if (methodName && strcmp(methodName, IARM_BUS_MFRLIB_API_GetSerializedData) == 0) {
                    auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                    if (param->type == mfrSERIALIZED_TYPE_SERIALNUMBER) {
                        strncpy(param->buffer, "A", sizeof(param->buffer) - 1);
                        param->buffer[sizeof(param->buffer) - 1] = '\0';
                        param->bufLen = strlen(param->buffer);
                        return IARM_RESULT_SUCCESS;
                    }
                }
                return IARM_RESULT_INVALID_PARAM;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("serialnumber"), _T(""), response));
    EXPECT_EQ(response, _T("{\"serialnumber\":\"A\"}"));
}

TEST_F(DeviceInfoTest, Boundary_Sku_EmptyModelNum)
{
    std::ofstream file("/etc/device.properties");
    file << "MODEL_NUM=\n";
    file.close();

    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(Return(IARM_RESULT_INVALID_PARAM));

    EXPECT_CALL(*p_rfcApiImplMock, getRFCParameter(_, _, _))
        .WillRepeatedly(Return(WDMP_FAILURE));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("modelid"), _T(""), response));
}

TEST_F(DeviceInfoTest, Boundary_Make_SingleCharacter)
{
    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillRepeatedly(::testing::Invoke(
            [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                if (methodName && strcmp(methodName, IARM_BUS_MFRLIB_API_GetSerializedData) == 0) {
                    auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                    if (param->type == mfrSERIALIZED_TYPE_MANUFACTURER) {
                        strncpy(param->buffer, "X", sizeof(param->buffer) - 1);
                        param->buffer[sizeof(param->buffer) - 1] = '\0';
                        param->bufLen = strlen(param->buffer);
                        return IARM_RESULT_SUCCESS;
                    }
                }
                return IARM_RESULT_INVALID_PARAM;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("make"), _T(""), response));
    EXPECT_EQ(response, _T("{\"make\":\"X\"}"));
}

TEST_F(DeviceInfoTest, EdgeCase_ReleaseVersion_MissingFile)
{
    removeFile("/version.txt");

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("releaseversion"), _T(""), response));
    EXPECT_EQ(response, _T("{\"releaseversion\":\"99.99.0.0\"}"));
}

TEST_F(DeviceInfoTest, EdgeCase_MultipleIARMCallsSequential)
{
    // Test multiple sequential MFR calls - each call gets its own specific expectation
    // First call: serialnumber
    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call(_, _, _, _))
        .WillOnce(::testing::Invoke(
            [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                if (methodName && strcmp(methodName, IARM_BUS_MFRLIB_API_GetSerializedData) == 0) {
                    auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                    if (param->type == mfrSERIALIZED_TYPE_SERIALNUMBER) {
                        strncpy(param->buffer, "SERIAL001", sizeof(param->buffer) - 1);
                        param->buffer[sizeof(param->buffer) - 1] = '\0';
                        param->bufLen = strlen(param->buffer);
                        return IARM_RESULT_SUCCESS;
                    }
                }
                return IARM_RESULT_INVALID_PARAM;
            }))
        .WillOnce(::testing::Invoke(
            [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                if (methodName && strcmp(methodName, IARM_BUS_MFRLIB_API_GetSerializedData) == 0) {
                    auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                    if (param->type == mfrSERIALIZED_TYPE_MANUFACTURER) {
                        strncpy(param->buffer, "MAKE001", sizeof(param->buffer) - 1);
                        param->buffer[sizeof(param->buffer) - 1] = '\0';
                        param->bufLen = strlen(param->buffer);
                        return IARM_RESULT_SUCCESS;
                    }
                }
                return IARM_RESULT_INVALID_PARAM;
            }))
        .WillOnce(::testing::Invoke(
            [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                if (methodName && strcmp(methodName, IARM_BUS_MFRLIB_API_GetSerializedData) == 0) {
                    auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                    if (param->type == mfrSERIALIZED_TYPE_MANUFACTURER) {
                        strncpy(param->buffer, "BRAND001", sizeof(param->buffer) - 1);
                        param->buffer[sizeof(param->buffer) - 1] = '\0';
                        param->bufLen = strlen(param->buffer);
                        return IARM_RESULT_SUCCESS;
                    }
                }
                return IARM_RESULT_INVALID_PARAM;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("serialnumber"), _T(""), response));
    EXPECT_EQ(response, _T("{\"serialnumber\":\"SERIAL001\"}"));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("make"), _T(""), response));
    EXPECT_EQ(response, _T("{\"make\":\"MAKE001\"}"));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("brandname"), _T(""), response));
    EXPECT_EQ(response, _T("{\"brand\":\"BRAND001\"}"));
}

TEST_F(DeviceInfoTest, EthMac_Success)
{
    EXPECT_CALL(*p_wrapsImplMock, v_secure_popen(::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke(
            [&](const char* direction, const char* command, va_list args) {
                va_list args2;
                va_copy(args2, args);
                char strFmt[256];
                vsnprintf(strFmt, sizeof(strFmt), command, args2);
                va_end(args2);
                EXPECT_EQ(string(strFmt), string("/lib/rdk/getDeviceDetails.sh read eth_mac"));

                const char mac[] = "AA:BB:CC:DD:EE:FF\n";
                FILE* pipe = fmemopen((void*)mac, sizeof(mac) - 1, "r");
                return pipe;
            }));

    EXPECT_CALL(*p_wrapsImplMock, v_secure_pclose(::testing::_))
        .Times(1)
        .WillOnce(::testing::Return(0));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ethmac"), _T(""), response));
    EXPECT_EQ(response, string("{\"eth_mac\":\"AA:BB:CC:DD:EE:FF\"}"));
}

TEST_F(DeviceInfoTest, EthMac_Failure_PopenReturnsNull)
{
    EXPECT_CALL(*p_wrapsImplMock, v_secure_popen(::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Return(nullptr));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("ethmac"), _T(""), response));
}

TEST_F(DeviceInfoTest, EthMac_Success_NewlineStripped)
{
    EXPECT_CALL(*p_wrapsImplMock, v_secure_popen(::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke(
            [&](const char* direction, const char* command, va_list args) {
                const char mac[] = "11:22:33:44:55:66\n";
                char buffer[256];
                strncpy(buffer, mac, sizeof(buffer) - 1);
                return fmemopen(buffer, strlen(buffer), "r");
            }));

    EXPECT_CALL(*p_wrapsImplMock, v_secure_pclose(::testing::_))
        .Times(1)
        .WillOnce(::testing::Return(0));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ethmac"), _T(""), response));
    // Verify newline is stripped - should not end with \n in JSON
    EXPECT_EQ(response, string("{\"eth_mac\":\"11:22:33:44:55:66\"}"));
}

TEST_F(DeviceInfoTest, EthMac_Success_EmptyOutput)
{
    EXPECT_CALL(*p_wrapsImplMock, v_secure_popen(::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke(
            [&](const char* direction, const char* command, va_list args) {
                char buffer[1] = {0};
                return fmemopen(buffer, 0, "r");
            }));

    EXPECT_CALL(*p_wrapsImplMock, v_secure_pclose(::testing::_))
        .Times(1)
        .WillOnce(::testing::Return(0));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ethmac"), _T(""), response));
    EXPECT_EQ(response, string("{\"eth_mac\":\"\"}"));
}

TEST_F(DeviceInfoTest, EstbMac_Success)
{
    EXPECT_CALL(*p_wrapsImplMock, v_secure_popen(::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke(
            [&](const char* direction, const char* command, va_list args) {
                va_list args2;
                va_copy(args2, args);
                char strFmt[256];
                vsnprintf(strFmt, sizeof(strFmt), command, args2);
                va_end(args2);
                EXPECT_EQ(string(strFmt), string("/lib/rdk/getDeviceDetails.sh read estb_mac"));

                const char mac[] = "11:22:33:44:55:66\n";
                char buffer[256];
                memset(buffer, 0, sizeof(buffer));
                strncpy(buffer, mac, sizeof(buffer) - 1);
                FILE* pipe = fmemopen(buffer, strlen(buffer), "r");
                return pipe;
            }));

    EXPECT_CALL(*p_wrapsImplMock, v_secure_pclose(::testing::_))
        .Times(1)
        .WillOnce(::testing::Return(0));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("estbmac"), _T(""), response));
    EXPECT_EQ(response, string("{\"estb_mac\":\"11:22:33:44:55:66\"}"));
}

TEST_F(DeviceInfoTest, EstbMac_Failure_PopenReturnsNull)
{
    EXPECT_CALL(*p_wrapsImplMock, v_secure_popen(::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Return(nullptr));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("estbmac"), _T(""), response));
}

TEST_F(DeviceInfoTest, EstbMac_Success_NewlineStripped)
{
    EXPECT_CALL(*p_wrapsImplMock, v_secure_popen(::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke(
            [&](const char* direction, const char* command, va_list args) {
                const char mac[] = "AA:11:BB:22:CC:33\n";
                char buffer[256];
                strncpy(buffer, mac, sizeof(buffer) - 1);
                return fmemopen(buffer, strlen(buffer), "r");
            }));

    EXPECT_CALL(*p_wrapsImplMock, v_secure_pclose(::testing::_))
        .Times(1)
        .WillOnce(::testing::Return(0));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("estbmac"), _T(""), response));
    EXPECT_EQ(response, string("{\"estb_mac\":\"AA:11:BB:22:CC:33\"}"));
}

TEST_F(DeviceInfoTest, WifiMac_Success)
{
    EXPECT_CALL(*p_wrapsImplMock, v_secure_popen(::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke(
            [&](const char* direction, const char* command, va_list args) {
                va_list args2;
                va_copy(args2, args);
                char strFmt[256];
                vsnprintf(strFmt, sizeof(strFmt), command, args2);
                va_end(args2);
                EXPECT_EQ(string(strFmt), string("/lib/rdk/getDeviceDetails.sh read wifi_mac"));

                const char mac[] = "00:11:22:33:44:55\n";
                char buffer[256];
                memset(buffer, 0, sizeof(buffer));
                strncpy(buffer, mac, sizeof(buffer) - 1);
                FILE* pipe = fmemopen(buffer, strlen(buffer), "r");
                return pipe;
            }));

    EXPECT_CALL(*p_wrapsImplMock, v_secure_pclose(::testing::_))
        .Times(1)
        .WillOnce(::testing::Return(0));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("wifimac"), _T(""), response));
    EXPECT_EQ(response, string("{\"wifi_mac\":\"00:11:22:33:44:55\"}"));
}

TEST_F(DeviceInfoTest, WifiMac_Failure_PopenReturnsNull)
{
    EXPECT_CALL(*p_wrapsImplMock, v_secure_popen(::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Return(nullptr));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("wifimac"), _T(""), response));
}

TEST_F(DeviceInfoTest, WifiMac_Success_NewlineStripped)
{
    EXPECT_CALL(*p_wrapsImplMock, v_secure_popen(::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke(
            [&](const char* direction, const char* command, va_list args) {
                const char mac[] = "FF:EE:DD:CC:BB:AA\n";
                char buffer[256];
                strncpy(buffer, mac, sizeof(buffer) - 1);
                return fmemopen(buffer, strlen(buffer), "r");
            }));

    EXPECT_CALL(*p_wrapsImplMock, v_secure_pclose(::testing::_))
        .Times(1)
        .WillOnce(::testing::Return(0));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("wifimac"), _T(""), response));
    EXPECT_EQ(response, string("{\"wifi_mac\":\"FF:EE:DD:CC:BB:AA\"}"));
}

TEST_F(DeviceInfoTest, EstbIp_Success)
{
    EXPECT_CALL(*p_wrapsImplMock, v_secure_popen(::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke(
            [&](const char* direction, const char* command, va_list args) {
                va_list args2;
                va_copy(args2, args);
                char strFmt[256];
                vsnprintf(strFmt, sizeof(strFmt), command, args2);
                va_end(args2);
                EXPECT_EQ(string(strFmt), string("/lib/rdk/getDeviceDetails.sh read estb_ip"));

                const char ip[] = "192.168.1.100\n";
                char buffer[256];
                memset(buffer, 0, sizeof(buffer));
                strncpy(buffer, ip, sizeof(buffer) - 1);
                FILE* pipe = fmemopen(buffer, strlen(buffer), "r");
                return pipe;
            }));

    EXPECT_CALL(*p_wrapsImplMock, v_secure_pclose(::testing::_))
        .Times(1)
        .WillOnce(::testing::Return(0));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("estbip"), _T(""), response));
    EXPECT_EQ(response, string("{\"estb_ip\":\"192.168.1.100\"}"));
}

TEST_F(DeviceInfoTest, EstbIp_Failure_PopenReturnsNull)
{
    EXPECT_CALL(*p_wrapsImplMock, v_secure_popen(::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Return(nullptr));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("estbip"), _T(""), response));
}

TEST_F(DeviceInfoTest, EstbIp_Success_NewlineStripped)
{
    EXPECT_CALL(*p_wrapsImplMock, v_secure_popen(::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke(
            [&](const char* direction, const char* command, va_list args) {
                const char ip[] = "10.0.0.1\n";
                char buffer[256];
                strncpy(buffer, ip, sizeof(buffer) - 1);
                return fmemopen(buffer, strlen(buffer), "r");
            }));

    EXPECT_CALL(*p_wrapsImplMock, v_secure_pclose(::testing::_))
        .Times(1)
        .WillOnce(::testing::Return(0));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("estbip"), _T(""), response));
    EXPECT_EQ(response, string("{\"estb_ip\":\"10.0.0.1\"}"));
}

TEST_F(DeviceInfoTest, Information_Success)
{
    // Test that Information() returns the correct description string
    string info = plugin->Information();

    EXPECT_FALSE(info.empty());
    EXPECT_EQ(info, "The DeviceInfo plugin allows retrieving of various device-related information.");
}
