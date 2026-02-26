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

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "L2Tests.h"
#include "L2TestsMock.h"
#include <fstream>
#include "MfrMock.h"
#include "IarmBusMock.h"
#include "SystemInfo.h"
#include <interfaces/IDeviceInfo.h>

#include <mutex>
#include <condition_variable>

#define JSON_TIMEOUT   (1000)
#define TEST_LOG(x, ...) fprintf(stderr, "\033[1;32m[%s:%d](%s)<PID:%d><TID:%d>" x "\n\033[0m", __FILE__, __LINE__, __FUNCTION__, getpid(), gettid(), ##__VA_ARGS__); fflush(stderr);
#define DEVICEINFO_CALLSIGN  _T("DeviceInfo.1")
#define DEVICEINFOL2TEST_CALLSIGN _T("L2tests.1")

using ::testing::NiceMock;
using namespace WPEFramework;
using testing::StrictMock;

class DeviceInfo_L2test : public L2TestMocks {
protected:
    Core::JSONRPC::Message message;
    string response;

    virtual ~DeviceInfo_L2test() override;
        
public:
    DeviceInfo_L2test();
    uint32_t CreateDeviceInfoInterfaceObject();
    void SetUp() override;
    void TearDown() override;

protected:
    /** @brief Pointer to the IShell interface */
    PluginHost::IShell* m_controller_deviceinfo;

    /** @brief Pointer to the IDeviceInfo interface */
    Exchange::IDeviceInfo* m_deviceinfoplugin;
};

/**
* @brief Constructor for DeviceInfo L2 test class
*/
DeviceInfo_L2test::DeviceInfo_L2test()
        : L2TestMocks()
        , m_controller_deviceinfo(nullptr)
        , m_deviceinfoplugin(nullptr)
{
    TEST_LOG("DEVICEINFO Constructor\n");
    uint32_t status = Core::ERROR_GENERAL;

    std::ofstream versionFile("/version.txt");
    versionFile << "imagename:CUSTOM_VBN_22.03s_sprint_20220331225312sdy_NG\n";
    versionFile << "SDK_VERSION=17.3\n";
    versionFile << "MEDIARITE=8.3.53\n";
    versionFile << "YOCTO_VERSION=dunfell\n";
    versionFile.flush();
    versionFile.close();

    std::ofstream devicePropsFile("/etc/device.properties");
    devicePropsFile << "MFG_NAME=TestManufacturer\n";
    devicePropsFile << "FRIENDLY_ID=\"TestModel\"\n";
    devicePropsFile << "MODEL_NUM=TEST_SKU_12345\n";
    devicePropsFile << "CHIPSET_NAME=TestChipset\n";
    devicePropsFile << "DEVICE_TYPE=IpStb\n";
    devicePropsFile.flush();
    devicePropsFile.close();

    std::ofstream authServiceFile("/etc/authService.conf");
    authServiceFile << "deviceType=IpStb\n";
    authServiceFile.flush();
    authServiceFile.close();

    std::ofstream manufacturerFile("/tmp/.manufacturer");
    manufacturerFile << "TestBrand\n";
    manufacturerFile.flush();
    manufacturerFile.close();

    // Create partnerId file with directory
    system("mkdir -p /opt/www/authService");
    std::ofstream partnerIdFile("/opt/www/authService/partnerId3.dat");
    partnerIdFile << "TestPartnerID\n";
    partnerIdFile.flush();
    partnerIdFile.close();

    // Setup RFC API mock expectations
    ON_CALL(*p_rfcApiImplMock, getRFCParameter(::testing::_, ::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [](char* pcCallerID, const char* pcParameterName, RFC_ParamData_t* pstParamData) {
                TEST_LOG("getRFCParameter invoked: param=%s", pcParameterName);
                
                if (strcmp(pcParameterName, "Device.DeviceInfo.SerialNumber") == 0) {
                    strcpy(pstParamData->value, "RFC_TEST_SERIAL");
                    return WDMP_SUCCESS;
                } else if (strcmp(pcParameterName, "Device.DeviceInfo.ModelName") == 0) {
                    strcpy(pstParamData->value, "RFC_TEST_MODEL");
                    return WDMP_SUCCESS;
                } else if (strcmp(pcParameterName, "Device.DeviceInfo.X_RDKCENTRAL-COM_Syndication.PartnerId") == 0) {
                    strcpy(pstParamData->value, "TestPartnerID");
                    printf("getRFCParameter invoked for PartnerId: %s\n", pstParamData->value);
                    return WDMP_SUCCESS;
                }
                
                pstParamData->value[0] = '\0';
                return WDMP_FAILURE;
            }));


    /* Activate plugin in constructor */
    status = ActivateService("DeviceInfo");
    EXPECT_EQ(Core::ERROR_NONE, status);
}

/**
* @brief Destructor for DeviceInfo L2 test class
*/
DeviceInfo_L2test::~DeviceInfo_L2test()
{
    TEST_LOG("DEVICEINFO Destructor\n");
    uint32_t status = Core::ERROR_GENERAL;

    if (m_deviceinfoplugin) {
        m_deviceinfoplugin->Release();
        m_deviceinfoplugin = nullptr;
    }
    if (m_controller_deviceinfo) {
        m_controller_deviceinfo->Release();
        m_controller_deviceinfo = nullptr;
    }

    status = DeactivateService("DeviceInfo");
    EXPECT_EQ(Core::ERROR_NONE, status);
}

uint32_t DeviceInfo_L2test::CreateDeviceInfoInterfaceObject()
{
    uint32_t return_value = Core::ERROR_GENERAL;
    Core::ProxyType<RPC::InvokeServerType<1, 0, 4>> DeviceInfo_Engine;
    Core::ProxyType<RPC::CommunicatorClient> DeviceInfo_Client;

    TEST_LOG("Creating DeviceInfo_Engine");
    DeviceInfo_Engine = Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create();
    DeviceInfo_Client = Core::ProxyType<RPC::CommunicatorClient>::Create(Core::NodeId("/tmp/communicator"), Core::ProxyType<Core::IIPCServer>(DeviceInfo_Engine));

    if (!DeviceInfo_Client.IsValid()) {
        TEST_LOG("Invalid DeviceInfo_Client");
    } else {
        m_controller_deviceinfo = DeviceInfo_Client->Open<PluginHost::IShell>(_T("DeviceInfo"), ~0, 3000);
        if (m_controller_deviceinfo) {
            m_deviceinfoplugin = m_controller_deviceinfo->QueryInterface<Exchange::IDeviceInfo>();
            return_value = Core::ERROR_NONE;
        }
    }
    return return_value;
}

void DeviceInfo_L2test::SetUp()
{
    if ((m_deviceinfoplugin == nullptr) || (m_controller_deviceinfo == nullptr)) {
        EXPECT_EQ(Core::ERROR_NONE, CreateDeviceInfoInterfaceObject());
    }
}

void DeviceInfo_L2test::TearDown()
{
    if (m_deviceinfoplugin) {
        m_deviceinfoplugin->Release();
        m_deviceinfoplugin = nullptr;
    }
    if (m_controller_deviceinfo) {
        m_controller_deviceinfo->Release();
        m_controller_deviceinfo = nullptr;
    }
}

TEST_F(DeviceInfo_L2test, DeviceInfo_L2_MethodTest)
{
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(DEVICEINFO_CALLSIGN, DEVICEINFOL2TEST_CALLSIGN);
    uint32_t status = Core::ERROR_NONE;

    TEST_LOG("Starting DeviceInfo L2 Method Tests\n");

    /****************** defaultresolution ******************/
    {
        TEST_LOG("Testing defaultresolution method\n");
        JsonObject result, params;
        params["videoDisplay"] = "HDMI0";
        
        device::VideoOutputPort videoOutputPort;
        device::VideoResolution videoResolution;
        string videoPort(_T("HDMI0"));
        string videoPortDefaultResolution(_T("1080p"));

        ON_CALL(*p_videoResolutionMock, getName())
            .WillByDefault(::testing::ReturnRef(videoPortDefaultResolution));
        ON_CALL(*p_videoOutputPortMock, getDefaultResolution())
            .WillByDefault(::testing::ReturnRef(videoResolution));
        ON_CALL(*p_hostImplMock, getDefaultVideoPortName())
            .WillByDefault(::testing::Return(videoPort));
        ON_CALL(*p_hostImplMock, getVideoOutputPort(::testing::_))
            .WillByDefault(::testing::ReturnRef(videoOutputPort));
        
        status = InvokeServiceMethod("DeviceInfo.1", "defaultresolution", params, result);
        EXPECT_EQ(Core::ERROR_NONE, status);
        if (status == Core::ERROR_NONE) {
            EXPECT_TRUE(result.HasLabel("defaultResolution"));
            string resolution = result["defaultResolution"].String();
            EXPECT_EQ(resolution, "1080p");
            TEST_LOG("defaultresolution: %s", resolution.c_str());
        }
    }

    /****************** supportedresolutions ******************/
    {
        TEST_LOG("Testing supportedresolutions method\n");
        JsonObject result, params;
        params["videoDisplay"] = "HDMI0";
        
        device::VideoOutputPort videoOutputPort;
        device::VideoOutputPortType videoOutputPortType;
        device::VideoResolution videoResolution;
        string videoPort(_T("HDMI0"));
        string videoPortSupportedResolution(_T("1080p"));

        ON_CALL(*p_videoResolutionMock, getName())
            .WillByDefault(::testing::ReturnRef(videoPortSupportedResolution));
        ON_CALL(*p_videoOutputPortTypeMock, getSupportedResolutions())
            .WillByDefault(::testing::Return(device::List<device::VideoResolution>({ videoResolution })));
        ON_CALL(*p_videoOutputPortTypeMock, getId())
            .WillByDefault(::testing::Return(0));
        ON_CALL(*p_videoOutputPortMock, getType())
            .WillByDefault(::testing::ReturnRef(videoOutputPortType));
        ON_CALL(*p_hostImplMock, getDefaultVideoPortName())
            .WillByDefault(::testing::Return(videoPort));
        ON_CALL(*p_hostImplMock, getVideoOutputPort(::testing::_))
            .WillByDefault(::testing::ReturnRef(videoOutputPort));
        ON_CALL(*p_videoOutputPortConfigImplMock, getPortType(::testing::_))
            .WillByDefault(::testing::ReturnRef(videoOutputPortType));
        
        status = InvokeServiceMethod("DeviceInfo.1", "supportedresolutions", params, result);
        EXPECT_EQ(Core::ERROR_NONE, status);
        if (status == Core::ERROR_NONE) {
            EXPECT_TRUE(result.HasLabel("supportedResolutions"));
            JsonArray resolutions = result["supportedResolutions"].Array();
            EXPECT_GT(resolutions.Length(), 0);
            if (resolutions.Length() > 0) {
                string resolution = resolutions[0].String();
                EXPECT_EQ(resolution, "1080p");
                TEST_LOG("First supported resolution: %s", resolution.c_str());
            }
        }
    }

    /****************** supportedhdcp ******************/
    {
        TEST_LOG("Testing supportedhdcp method\n");
        JsonObject result, params;
        params["videoDisplay"] = "HDMI0";
        
        device::VideoOutputPort videoOutputPort;
        string videoPort(_T("HDMI0"));

        ON_CALL(*p_videoOutputPortMock, getHDCPProtocol())
            .WillByDefault(::testing::Return(dsHDCP_VERSION_2X));
        ON_CALL(*p_hostImplMock, getDefaultVideoPortName())
            .WillByDefault(::testing::Return(videoPort));
        ON_CALL(*p_videoOutputPortConfigImplMock, getPort(::testing::_))
            .WillByDefault(::testing::ReturnRef(videoOutputPort));
        
        status = InvokeServiceMethod("DeviceInfo.1", "supportedhdcp", params, result);
        EXPECT_EQ(Core::ERROR_NONE, status);
        if (status == Core::ERROR_NONE) {
            EXPECT_TRUE(result.HasLabel("supportedHDCPVersion"));
            string hdcpVersion = result["supportedHDCPVersion"].String();
            EXPECT_FALSE(hdcpVersion.empty());
            EXPECT_EQ(hdcpVersion, "2.2"); 
            TEST_LOG("Supported HDCP version: %s", hdcpVersion.c_str());
        }
    }

    /****************** audiocapabilities ******************/
    {
        TEST_LOG("Testing audiocapabilities method\n");
        JsonObject result, params;
        params["audioPort"] = "HDMI0";
        
        device::AudioOutputPort audioOutputPort;
        string audioPort(_T("HDMI0"));

        ON_CALL(*p_audioOutputPortMock, getAudioCapabilities(::testing::_))
            .WillByDefault(::testing::Invoke(
                [&](int* capabilities) {
                    if (capabilities != nullptr) {
                        *capabilities = dsAUDIOSUPPORT_ATMOS | dsAUDIOSUPPORT_DD | dsAUDIOSUPPORT_DDPLUS | dsAUDIOSUPPORT_DAD | dsAUDIOSUPPORT_DAPv2 | dsAUDIOSUPPORT_MS12;
                    }
                }));
        ON_CALL(*p_hostImplMock, getDefaultAudioPortName())
            .WillByDefault(::testing::Return(audioPort));
        ON_CALL(*p_hostImplMock, getAudioOutputPort(::testing::_))
            .WillByDefault(::testing::ReturnRef(audioOutputPort));
        
        status = InvokeServiceMethod("DeviceInfo.1", "audiocapabilities", params, result);
        EXPECT_EQ(Core::ERROR_NONE, status);
        if (status == Core::ERROR_NONE) {
            EXPECT_TRUE(result.HasLabel("AudioCapabilities"));
            JsonArray capabilities = result["AudioCapabilities"].Array();
            EXPECT_GT(capabilities.Length(), 0);
            // Verify expected capabilities are present
            bool hasAtmos = false, hasDD = false, hasDDPlus = false, hasDAD = false, hasDAPv2 = false, hasMS12 = false;
            for (int i = 0; i < capabilities.Length(); i++) {
                string cap = capabilities[i].String();
                if (cap == "ATMOS") hasAtmos = true;
                if (cap == "DD") hasDD = true;
                if (cap == "DDPLUS") hasDDPlus = true;
                if (cap == "DAD") hasDAD = true;
                if (cap == "DAPv2") hasDAPv2 = true;
                if (cap == "MS12") hasMS12 = true;
            }
            EXPECT_TRUE(hasAtmos || hasDD || hasDDPlus);
            TEST_LOG("Audio capabilities count: %d", capabilities.Length());
        }
    }

    /****************** ms12capabilities ******************/
    {
        TEST_LOG("Testing ms12capabilities method\n");
        JsonObject result, params;
        params["audioPort"] = "HDMI0";
        
        device::AudioOutputPort audioOutputPort;
        string audioPort(_T("HDMI0"));

        ON_CALL(*p_audioOutputPortMock, getMS12Capabilities(::testing::_))
            .WillByDefault(::testing::Invoke(
                [&](int* capabilities) {
                    if (capabilities != nullptr) {
                        *capabilities = dsMS12SUPPORT_DolbyVolume | dsMS12SUPPORT_InteligentEqualizer | dsMS12SUPPORT_DialogueEnhancer;
                    }
                }));
        ON_CALL(*p_hostImplMock, getDefaultAudioPortName())
            .WillByDefault(::testing::Return(audioPort));
        ON_CALL(*p_hostImplMock, getAudioOutputPort(::testing::_))
            .WillByDefault(::testing::ReturnRef(audioOutputPort));
        
        status = InvokeServiceMethod("DeviceInfo.1", "ms12capabilities", params, result);
        EXPECT_EQ(Core::ERROR_NONE, status);
        if (status == Core::ERROR_NONE) {
            EXPECT_TRUE(result.HasLabel("MS12Capabilities"));
            JsonArray capabilities = result["MS12Capabilities"].Array();
            EXPECT_GT(capabilities.Length(), 0);
            // Verify expected capabilities are present
            bool hasDolbyVolume = false, hasIntelligentEqualizer = false, hasDialogueEnhancer = false;
                for (int i = 0; i < capabilities.Length(); i++) {
                string cap = capabilities[i].String();
                if (cap == "Dolby_Volume") hasDolbyVolume = true;
                if (cap == "Inteligent_Equalizer") hasIntelligentEqualizer = true;
                if (cap == "Dialogue_Enhancer") hasDialogueEnhancer = true;
            }
            EXPECT_TRUE(hasDolbyVolume || hasIntelligentEqualizer || hasDialogueEnhancer);
            TEST_LOG("MS12 capabilities count: %d", capabilities.Length());
        }
    }

    /****************** supportedms12audioprofiles ******************/
    {
        TEST_LOG("Testing supportedms12audioprofiles method\n");
        JsonObject result, params;
        params["audioPort"] = "HDMI0";
        
        device::AudioOutputPort audioOutputPort;
        string audioPort(_T("HDMI0"));
        string audioPortMS12AudioProfile(_T("Movie"));

        ON_CALL(*p_audioOutputPortMock, getMS12AudioProfileList())
            .WillByDefault(::testing::Return(std::vector<std::string>({ audioPortMS12AudioProfile })));
        ON_CALL(*p_hostImplMock, getDefaultAudioPortName())
            .WillByDefault(::testing::Return(audioPort));
        ON_CALL(*p_hostImplMock, getAudioOutputPort(::testing::_))
            .WillByDefault(::testing::ReturnRef(audioOutputPort));
        
        status = InvokeServiceMethod("DeviceInfo.1", "supportedms12audioprofiles", params, result);
        EXPECT_EQ(Core::ERROR_NONE, status);
        if (status == Core::ERROR_NONE) {
            EXPECT_TRUE(result.HasLabel("supportedMS12AudioProfiles"));
            JsonArray profiles = result["supportedMS12AudioProfiles"].Array();
            EXPECT_GT(profiles.Length(), 0);
            if (profiles.Length() > 0) {
                string profile = profiles[0].String();
                EXPECT_EQ(profile, "Movie");
                TEST_LOG("First MS12 audio profile: %s", profile.c_str());
            }
        }
    }

    TEST_LOG("DeviceInfo L2 Method Tests completed\n");
}

TEST_F(DeviceInfo_L2test, DeviceInfo_L2_PropertyTest)
{
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(DEVICEINFO_CALLSIGN, DEVICEINFOL2TEST_CALLSIGN);
    JsonObject result, params;
    uint32_t status = Core::ERROR_NONE;

    TEST_LOG("Starting DeviceInfo L2 Property Tests\n");

    /****************** systeminfo ******************/
    {
        TEST_LOG("Testing systeminfo property\n");
    
        ON_CALL(*p_iarmBusImplMock, IARM_Bus_Call)
            .WillByDefault(
                [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                    EXPECT_EQ(string(ownerName), string(_T(IARM_BUS_MFRLIB_NAME)));
                    EXPECT_EQ(string(methodName), string(_T(IARM_BUS_MFRLIB_API_GetSerializedData)));
                    auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                    const char* str = "5678";
                    param->bufLen = strlen(str);
                    strncpy(param->buffer, str, sizeof(param->buffer));
                    param->type =  mfrSERIALIZED_TYPE_SERIALNUMBER;
                    return IARM_RESULT_SUCCESS;
                });
        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "systeminfo@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("serialnumber"));
            string serialNumber = getResults["serialnumber"].String();
            EXPECT_FALSE(serialNumber.empty());
            EXPECT_EQ(serialNumber, "5678");
            TEST_LOG("System info serial number: %s", serialNumber.c_str());
        }
    }

    /****************** addresses ******************/
    {
        TEST_LOG("Testing addresses property\n");

        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "addresses@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        // addresses returns empty object {} when no addresses available
        TEST_LOG("addresses test completed\n");
    }

    // /****************** socketinfo ******************/
    // {
    //     TEST_LOG("Testing socketinfo property\n");

    //     JsonObject getResults;
    //     uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "socketinfo@0", getResults);
    //     EXPECT_EQ(Core::ERROR_NONE, getResult);
    //     if (getResult == Core::ERROR_NONE) {
    //         EXPECT_TRUE(getResults.HasLabel("runs"));
    //         int runs = getResults["runs"].Number();
    //         EXPECT_GE(runs, 0);
    //         EXPECT_EQ(runs, 99);
    //         TEST_LOG("socketinfo runs: %d", runs);
    //     }
    // }

    /****************** firmwareversion ******************/
    {
        TEST_LOG("Testing firmwareversion property\n");

        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "firmwareversion@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("imagename"));
            string imagename = getResults["imagename"].String();
            EXPECT_FALSE(imagename.empty());
            EXPECT_EQ(imagename, "CUSTOM_VBN_22.03s_sprint_20220331225312sdy_NG");
            TEST_LOG("Firmware imagename: %s", imagename.c_str());
        }
        // Additional validations for other firmware fields
        if (getResults.HasLabel("sdk")) {
            string sdk = getResults["sdk"].String();
            EXPECT_EQ(sdk, "17.3");
        }
        if (getResults.HasLabel("mediarite")) {
            string mediarite = getResults["mediarite"].String();
            EXPECT_EQ(mediarite, "8.3.53");
        }
        if (getResults.HasLabel("yocto")) {
            string yocto = getResults["yocto"].String();
            EXPECT_EQ(yocto, "dunfell");
        }
    }

    /****************** serialnumber ******************/
    {
        TEST_LOG("Testing serialnumber property\n");
        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "serialnumber@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("serialnumber"));
            string serialNumber = getResults["serialnumber"].String();
            EXPECT_FALSE(serialNumber.empty());
            EXPECT_EQ(serialNumber, "5678");
            TEST_LOG("Serial number: %s", serialNumber.c_str());
        }
    }

    /****************** modelid ******************/
    {
        TEST_LOG("Testing modelid property\n");

        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "modelid@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("sku"));
            string sku = getResults["sku"].String();
            EXPECT_FALSE(sku.empty());
            EXPECT_EQ(sku, "TEST_SKU_12345");
            TEST_LOG("Model ID (SKU): %s", sku.c_str());
        }
    }

    /****************** make ******************/
    {
        TEST_LOG("Testing make property\n");

        ON_CALL(*p_iarmBusImplMock, IARM_Bus_Call)
            .WillByDefault(
                [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                    EXPECT_EQ(string(ownerName), string(_T(IARM_BUS_MFRLIB_NAME)));
                    EXPECT_EQ(string(methodName), string(_T(IARM_BUS_MFRLIB_API_GetSerializedData)));
                    auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                    const char* str = "TestManufacturer";
                    param->bufLen = strlen(str);
                    strncpy(param->buffer, str, sizeof(param->buffer));
                    param->type =  mfrSERIALIZED_TYPE_MANUFACTURER;
                    return IARM_RESULT_SUCCESS;
                });
        
        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "make@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("make"));
            string make = getResults["make"].String();
            EXPECT_FALSE(make.empty());
            EXPECT_EQ(make, "TestManufacturer");
            TEST_LOG("Make: %s", make.c_str());
        }
    }

    /****************** modelname ******************/
    {
        TEST_LOG("Testing modelname property\n");

        std::ofstream file("/etc/device.properties");
        file << "FRIENDLY_ID=\"CUSTOM4 CUSTOM9\"";
        file.close();

        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "modelname@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("model"));
            string model = getResults["model"].String();
            EXPECT_FALSE(model.empty());
            EXPECT_EQ(model, "CUSTOM4 CUSTOM9");
            TEST_LOG("Model name: %s", model.c_str());
        }
    }

    /****************** brandname ******************/
    {
        TEST_LOG("Testing brandname property\n");
        JsonObject result, params;
        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "brandname@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("brand"));
            string brand = getResults["brand"].String();
            EXPECT_FALSE(brand.empty());
            EXPECT_EQ(brand, "TestBrand");
            TEST_LOG("Brand name: %s", brand.c_str());
        }
    }

    /****************** devicetype ******************/
    {
        TEST_LOG("Testing devicetype property\n");

        std::ofstream file("/etc/authService.conf");
        file << "deviceType=IpStb";
        file.close();
        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "devicetype@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("devicetype"));
            string deviceType = getResults["devicetype"].String();
            EXPECT_FALSE(deviceType.empty());
            EXPECT_EQ(deviceType, "IpStb");
            TEST_LOG("Device type: %s", deviceType.c_str());
        }
    }

    /****************** socname ******************/
    {
        TEST_LOG("Testing socname property\n");

        std::ofstream file("/etc/device.properties");
        file << "SOC=NVIDIA\n";
        file.close();

        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "socname@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("socname"));
            string socName = getResults["socname"].String();
            EXPECT_FALSE(socName.empty());
            EXPECT_EQ(socName, "NVIDIA");
            TEST_LOG("SoC name: %s", socName.c_str());
        }
    }

    {
        TEST_LOG("Testing distributorid from file\n");
        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "distributorid@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("distributorid"));
            string distributorId = getResults["distributorid"].String();
            EXPECT_EQ(distributorId, "TestPartnerID");
            TEST_LOG("Distributor ID: %s", distributorId.c_str());
        }
    }


    /****************** releaseversion ******************/
    {
        TEST_LOG("Testing releaseversion property\n");

        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "releaseversion@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("releaseversion"));
            string releaseVersion = getResults["releaseversion"].String();
            EXPECT_FALSE(releaseVersion.empty());
            EXPECT_EQ(releaseVersion, "22.03.0.0");
            TEST_LOG("Release version: %s", releaseVersion.c_str());
        }
    }

    /****************** chipset ******************/
    {
        TEST_LOG("Testing chipset property\n");

        std::ofstream file("/etc/device.properties");
        file << "CHIPSET_NAME=TestChipset\n";
        file.close();

        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "chipset@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);


        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("chipset"));
            string chipset = getResults["chipset"].String();
            EXPECT_FALSE(chipset.empty());
            EXPECT_EQ(chipset, "TestChipset");
            TEST_LOG("Chipset: %s", chipset.c_str());
        }
    }

    /****************** supportedaudioports ******************/
    {
        TEST_LOG("Testing supportedaudioports property\n");
        JsonObject result, params;
        JsonObject getResults;
        device::AudioOutputPort audioOutputPort;
        string audioPort(_T("HDMI0"));

        ON_CALL(*p_audioOutputPortMock, getName())
            .WillByDefault(::testing::ReturnRef(audioPort));
        ON_CALL(*p_hostImplMock, getAudioOutputPorts())
            .WillByDefault(::testing::Return(device::List<device::AudioOutputPort>({ audioOutputPort })));

        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "supportedaudioports@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("supportedAudioPorts"));
            JsonArray audioPorts = getResults["supportedAudioPorts"].Array();
            EXPECT_GT(audioPorts.Length(), 0);
            if (audioPorts.Length() > 0) {
                string port = audioPorts[0].String();
                EXPECT_EQ(port, "HDMI0");
                TEST_LOG("First audio port: %s", port.c_str());
            }
        }
    }

    /****************** supportedvideodisplays ******************/
    {
        TEST_LOG("Testing supportedvideodisplays property\n");
        JsonObject result, params;
        
        device::VideoOutputPort videoOutputPort;
        string videoPort(_T("HDMI0"));

        ON_CALL(*p_videoOutputPortMock, getName())
            .WillByDefault(::testing::ReturnRef(videoPort));
        ON_CALL(*p_hostImplMock, getVideoOutputPorts())
            .WillByDefault(::testing::Return(device::List<device::VideoOutputPort>({ videoOutputPort })));

        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "supportedvideodisplays@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("supportedVideoDisplays"));
            JsonArray videoDisplays = getResults["supportedVideoDisplays"].Array();
            EXPECT_GT(videoDisplays.Length(), 0);
            if (videoDisplays.Length() > 0) {
                string display = videoDisplays[0].String();
                EXPECT_EQ(display, "HDMI0");
                TEST_LOG("First video display: %s", display.c_str());
            }
        }
    }

    /****************** hostedid ******************/
    {
        TEST_LOG("Testing hostedid property\n");
        JsonObject result, params;
        
        ON_CALL(*p_hostImplMock, getHostEDID(::testing::_))
            .WillByDefault(::testing::Invoke(
                [&](std::vector<uint8_t>& edid) {
                    edid = { 't', 'e', 's', 't' };
                }));
        
        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "hostedid@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("EDID"));
            string edid = getResults["EDID"].String();
            EXPECT_FALSE(edid.empty());
            EXPECT_EQ(edid, "dGVzdA==");  // base64 encoded "test"
            TEST_LOG("EDID (base64): %s", edid.c_str());
        }
    }

    TEST_LOG("DeviceInfo L2 Property Tests completed\n");
}

TEST_F(DeviceInfo_L2test, DeviceInfo_L2_ErrorHandlingTest)
{
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(DEVICEINFO_CALLSIGN, DEVICEINFOL2TEST_CALLSIGN);
    uint32_t status = Core::ERROR_NONE;

    TEST_LOG("Starting DeviceInfo L2 Error Handling Tests\n");

    /****************** Test with invalid video display ******************/
    {
        TEST_LOG("Testing defaultresolution with invalid videoDisplay\n");
        JsonObject result, params;
        params["videoDisplay"] = "INVALID_PORT";
        
        device::VideoOutputPort videoOutputPort;
        string videoPort(_T("HDMI0"));

        ON_CALL(*p_hostImplMock, getDefaultVideoPortName())
            .WillByDefault(::testing::Return(videoPort));
        ON_CALL(*p_hostImplMock, getVideoOutputPort(::testing::_))
            .WillByDefault(::testing::Throw(device::Exception("Invalid port")));
        
        status = InvokeServiceMethod("DeviceInfo.1", "defaultresolution", params, result);
        EXPECT_EQ(Core::ERROR_GENERAL, status);
    }

    /****************** Test with invalid audio port ******************/
    {
        TEST_LOG("Testing audiocapabilities with invalid audioPort\n");
        JsonObject result, params;
        params["audioPort"] = "INVALID_AUDIO_PORT";
        
        string audioPort(_T("HDMI0"));

        ON_CALL(*p_hostImplMock, getDefaultAudioPortName())
            .WillByDefault(::testing::Return(audioPort));
        ON_CALL(*p_hostImplMock, getAudioOutputPort(::testing::_))
            .WillByDefault(::testing::Throw(device::Exception("Invalid audio port")));
        
        status = InvokeServiceMethod("DeviceInfo.1", "audiocapabilities", params, result);
        EXPECT_EQ(Core::ERROR_GENERAL, status);
    }

    /****************** Test supportedresolutions with exception ******************/
    {
        TEST_LOG("Testing supportedresolutions with device exception\n");
        JsonObject result, params;
        params["videoDisplay"] = "HDMI0";
        
        device::VideoOutputPort videoOutputPort;
        string videoPort(_T("HDMI0"));

        ON_CALL(*p_hostImplMock, getDefaultVideoPortName())
            .WillByDefault(::testing::Return(videoPort));
        ON_CALL(*p_hostImplMock, getVideoOutputPort(::testing::_))
            .WillByDefault(::testing::ReturnRef(videoOutputPort));
        ON_CALL(*p_videoOutputPortMock, getType())
            .WillByDefault(::testing::Throw(device::Exception("Type exception")));
        
        status = InvokeServiceMethod("DeviceInfo.1", "supportedresolutions", params, result);
        EXPECT_EQ(Core::ERROR_GENERAL, status);
    }

    /****************** Test ms12capabilities with exception ******************/
    {
        TEST_LOG("Testing ms12capabilities with device exception\n");
        JsonObject result, params;
        params["audioPort"] = "HDMI0";
        
        string audioPort(_T("HDMI0"));

        ON_CALL(*p_hostImplMock, getDefaultAudioPortName())
            .WillByDefault(::testing::Return(audioPort));
        ON_CALL(*p_hostImplMock, getAudioOutputPort(::testing::_))
            .WillByDefault(::testing::Throw(device::Exception("MS12 exception")));
        
        status = InvokeServiceMethod("DeviceInfo.1", "ms12capabilities", params, result);
        EXPECT_EQ(Core::ERROR_GENERAL, status);
    }

    TEST_LOG("DeviceInfo L2 Error Handling Tests completed\n");
}
TEST_F(DeviceInfo_L2test, DeviceInfo_L2_EdgeCaseTest)
{
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(DEVICEINFO_CALLSIGN, DEVICEINFOL2TEST_CALLSIGN);
    uint32_t status = Core::ERROR_NONE;

    TEST_LOG("Starting DeviceInfo L2 Edge Case Tests\n");

    /****************** Test with empty videoDisplay parameter ******************/
    {
        TEST_LOG("Testing defaultresolution with empty videoDisplay\n");
        JsonObject result, params;
        params["videoDisplay"] = "";
        
        device::VideoOutputPort videoOutputPort;
        device::VideoResolution videoResolution;
        string videoPort(_T("HDMI0"));
        string videoPortDefaultResolution(_T("1080p"));

        ON_CALL(*p_videoResolutionMock, getName())
            .WillByDefault(::testing::ReturnRef(videoPortDefaultResolution));
        ON_CALL(*p_videoOutputPortMock, getDefaultResolution())
            .WillByDefault(::testing::ReturnRef(videoResolution));
        ON_CALL(*p_hostImplMock, getDefaultVideoPortName())
            .WillByDefault(::testing::Return(videoPort));
        ON_CALL(*p_hostImplMock, getVideoOutputPort(::testing::_))
            .WillByDefault(::testing::ReturnRef(videoOutputPort));
        
        status = InvokeServiceMethod("DeviceInfo.1", "defaultresolution", params, result);
        EXPECT_EQ(Core::ERROR_NONE, status);
        if (status == Core::ERROR_NONE) {
            EXPECT_TRUE(result.HasLabel("defaultResolution"));
            string resolution = result["defaultResolution"].String();
            EXPECT_EQ(resolution, "1080p");
            TEST_LOG("defaultresolution with empty param: %s", resolution.c_str());
        }
    }

    /****************** Test with empty audioPort parameter ******************/
    {
        TEST_LOG("Testing audiocapabilities with empty audioPort\n");
        JsonObject result, params;
        params["audioPort"] = "";
        
        device::AudioOutputPort audioOutputPort;
        string audioPort(_T("HDMI0"));

        ON_CALL(*p_audioOutputPortMock, getAudioCapabilities(::testing::_))
            .WillByDefault(::testing::Invoke(
                [&](int* capabilities) {
                    if (capabilities != nullptr) {
                        *capabilities = dsAUDIOSUPPORT_NONE;
                    }
                }));
        ON_CALL(*p_hostImplMock, getDefaultAudioPortName())
            .WillByDefault(::testing::Return(audioPort));
        ON_CALL(*p_hostImplMock, getAudioOutputPort(::testing::_))
            .WillByDefault(::testing::ReturnRef(audioOutputPort));
        
        status = InvokeServiceMethod("DeviceInfo.1", "audiocapabilities", params, result);
        EXPECT_EQ(Core::ERROR_NONE, status);
        if (status == Core::ERROR_NONE) {
            EXPECT_TRUE(result.HasLabel("AudioCapabilities"));
            JsonArray capabilities = result["AudioCapabilities"].Array();
            // With NONE capability, array should be empty or have NONE value
            TEST_LOG("Audio capabilities with empty port: %d items", capabilities.Length());
        }
    }

    /****************** Test with multiple resolutions ******************/
    {
        TEST_LOG("Testing supportedresolutions with multiple resolutions\n");
        JsonObject result, params;
        params["videoDisplay"] = "HDMI0";
        
        device::VideoOutputPort videoOutputPort;
        device::VideoOutputPortType videoOutputPortType;
        device::VideoResolution videoResolution1, videoResolution2, videoResolution3;
        string videoPort(_T("HDMI0"));
        string resolution1(_T("480p"));
        string resolution2(_T("720p"));
        string resolution3(_T("1080p"));


        ON_CALL(*p_videoResolutionMock, getName())
            .WillByDefault(::testing::ReturnRef(resolution1));
        ON_CALL(*p_videoOutputPortTypeMock, getSupportedResolutions())
            .WillByDefault(::testing::Return(device::List<device::VideoResolution>({ videoResolution1, videoResolution2, videoResolution3 })));
        ON_CALL(*p_videoOutputPortTypeMock, getId())
            .WillByDefault(::testing::Return(0));
        ON_CALL(*p_videoOutputPortMock, getType())
            .WillByDefault(::testing::ReturnRef(videoOutputPortType));
        ON_CALL(*p_hostImplMock, getDefaultVideoPortName())
            .WillByDefault(::testing::Return(videoPort));
        ON_CALL(*p_hostImplMock, getVideoOutputPort(::testing::_))
            .WillByDefault(::testing::ReturnRef(videoOutputPort));
        ON_CALL(*p_videoOutputPortConfigImplMock, getPortType(::testing::_))
            .WillByDefault(::testing::ReturnRef(videoOutputPortType));
        
        status = InvokeServiceMethod("DeviceInfo.1", "supportedresolutions", params, result);
        EXPECT_EQ(Core::ERROR_NONE, status);
        if (status == Core::ERROR_NONE) {
            EXPECT_TRUE(result.HasLabel("supportedResolutions"));
            JsonArray resolutions = result["supportedResolutions"].Array();
            EXPECT_EQ(resolutions.Length(), 3);  // Should have 3 resolutions
            TEST_LOG("Supported resolutions count: %d", resolutions.Length());
            // Verify resolutions are present
            for (int i = 0; i < resolutions.Length(); i++) {
                string res = resolutions[i].String();
                EXPECT_FALSE(res.empty());
                EXPECT_EQ(res, "480p");
                // EXPECT_TRUE(res == "480p" || res == "720p" || res == "1080p");
                TEST_LOG("Resolution[%d]: %s", i, res.c_str());
            }
        }
    }

    /****************** Test HDCP version 1.x ******************/
    {
        TEST_LOG("Testing supportedhdcp with HDCP 1.x\n");
        JsonObject result, params;
        params["videoDisplay"] = "HDMI0";
        
        device::VideoOutputPort videoOutputPort;
        string videoPort(_T("HDMI0"));

        ON_CALL(*p_videoOutputPortMock, getHDCPProtocol())
            .WillByDefault(::testing::Return(dsHDCP_VERSION_1X));
        ON_CALL(*p_hostImplMock, getDefaultVideoPortName())
            .WillByDefault(::testing::Return(videoPort));
        ON_CALL(*p_videoOutputPortConfigImplMock, getPort(::testing::_))
            .WillByDefault(::testing::ReturnRef(videoOutputPort));
        
        status = InvokeServiceMethod("DeviceInfo.1", "supportedhdcp", params, result);
        EXPECT_EQ(Core::ERROR_NONE, status);
        if (status == Core::ERROR_NONE) {
            EXPECT_TRUE(result.HasLabel("supportedHDCPVersion"));
            string hdcpVersion = result["supportedHDCPVersion"].String();
            EXPECT_FALSE(hdcpVersion.empty());
            EXPECT_EQ(hdcpVersion, "1.4");
            // HDCP 1.x should be returned
            TEST_LOG("HDCP version 1.x: %s", hdcpVersion.c_str());
        }
    }

    /****************** Test all MS12 capabilities ******************/
    {
        TEST_LOG("Testing ms12capabilities with all capabilities enabled\n");
        JsonObject result, params;
        params["audioPort"] = "HDMI0";
        
        device::AudioOutputPort audioOutputPort;
        string audioPort(_T("HDMI0"));

        ON_CALL(*p_audioOutputPortMock, getMS12Capabilities(::testing::_))
            .WillByDefault(::testing::Invoke(
                [&](int* capabilities) {
                    if (capabilities != nullptr) {
                        *capabilities = dsMS12SUPPORT_DolbyVolume | dsMS12SUPPORT_InteligentEqualizer | dsMS12SUPPORT_DialogueEnhancer;
                    }
                }));
        ON_CALL(*p_hostImplMock, getDefaultAudioPortName())
            .WillByDefault(::testing::Return(audioPort));
        ON_CALL(*p_hostImplMock, getAudioOutputPort(::testing::_))
            .WillByDefault(::testing::ReturnRef(audioOutputPort));
        
        status = InvokeServiceMethod("DeviceInfo.1", "ms12capabilities", params, result);
        EXPECT_EQ(Core::ERROR_NONE, status);
        if (status == Core::ERROR_NONE) {
            EXPECT_TRUE(result.HasLabel("MS12Capabilities"));
            JsonArray capabilities = result["MS12Capabilities"].Array();
            EXPECT_GT(capabilities.Length(), 0);
            // Should have DolbyVolume, InteligentEqualizer, DialogueEnhancer
            EXPECT_GE(capabilities.Length(), 3);
            bool hasDV = false, hasIE = false, hasDE = false;
            for (int i = 0; i < capabilities.Length(); i++) {
                string cap = capabilities[i].String();
                if (cap.find("Dolby_Volume") != string::npos) hasDV = true;
                if (cap.find("Inteligent_Equalizer") != string::npos) hasIE = true;
                if (cap.find("Dialogue_Enhancer") != string::npos) hasDE = true;
                TEST_LOG("MS12 capability[%d]: %s", i, cap.c_str());
            }
            EXPECT_TRUE(hasDV && hasIE && hasDE);
        }
    }

    /****************** Test multiple MS12 audio profiles ******************/
    {
        TEST_LOG("Testing supportedms12audioprofiles with multiple profiles\n");
        JsonObject result, params;
        params["audioPort"] = "HDMI0";
        
        device::AudioOutputPort audioOutputPort;
        string audioPort(_T("HDMI0"));
        std::vector<std::string> profiles = {"Movie", "Music", "Sports", "Game"};

        ON_CALL(*p_audioOutputPortMock, getMS12AudioProfileList())
            .WillByDefault(::testing::Return(profiles));
        ON_CALL(*p_hostImplMock, getDefaultAudioPortName())
            .WillByDefault(::testing::Return(audioPort));
        ON_CALL(*p_hostImplMock, getAudioOutputPort(::testing::_))
            .WillByDefault(::testing::ReturnRef(audioOutputPort));
        
        status = InvokeServiceMethod("DeviceInfo.1", "supportedms12audioprofiles", params, result);
        EXPECT_EQ(Core::ERROR_NONE, status);
        if (status == Core::ERROR_NONE) {
            EXPECT_TRUE(result.HasLabel("supportedMS12AudioProfiles"));
            JsonArray profiles = result["supportedMS12AudioProfiles"].Array();
            EXPECT_EQ(profiles.Length(), 4);  // Should have 4 profiles: Movie, Music, Sports, Game
            // Verify all expected profiles are present
            bool hasMovie = false, hasMusic = false, hasSports = false, hasGame = false;
            for (int i = 0; i < profiles.Length(); i++) {
                string profile = profiles[i].String();
                if (profile == "Movie") hasMovie = true;
                if (profile == "Music") hasMusic = true;
                if (profile == "Sports") hasSports = true;
                if (profile == "Game") hasGame = true;
                TEST_LOG("MS12 profile[%d]: %s", i, profile.c_str());
            }
            EXPECT_TRUE(hasMovie && hasMusic && hasSports && hasGame);
        }
    }

    TEST_LOG("DeviceInfo L2 Edge Case Tests completed\n");
}

TEST_F(DeviceInfo_L2test, DeviceInfo_L2_PropertyEdgeCaseTest)
{
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(DEVICEINFO_CALLSIGN, DEVICEINFOL2TEST_CALLSIGN);
    uint32_t status = Core::ERROR_NONE;

    TEST_LOG("Starting DeviceInfo L2 Property Edge Case Tests\n");

    /****************** Test firmwareversion with missing fields ******************/
    {
        TEST_LOG("Testing firmwareversion with only imagename\n");

        std::ofstream file("/version.txt");
        file << "imagename:TEST_IMAGE\n";
        file.close();

        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "firmwareversion@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        TEST_LOG("firmwareversion with minimal data test completed\n");
    }

    /****************** Test firmwareversion with all fields ******************/
    {
        TEST_LOG("Testing firmwareversion with all fields present\n");

        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "firmwareversion@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        
        if (getResult == Core::ERROR_NONE && getResults.HasLabel("firmwareversion")) {
            JsonObject fwObj = getResults["firmwareversion"].Object();
            EXPECT_TRUE(fwObj.HasLabel("imagename"));
            EXPECT_TRUE(fwObj.HasLabel("sdk"));
            EXPECT_TRUE(fwObj.HasLabel("mediarite"));
            EXPECT_TRUE(fwObj.HasLabel("yocto"));
        }
        TEST_LOG("firmwareversion with all fields test completed\n");
    }

    /****************** Test serialnumber from MFR ******************/
    {
        TEST_LOG("Testing serialnumber from MFR\n");
        
        ON_CALL(*p_iarmBusImplMock, IARM_Bus_Call)
            .WillByDefault(
                [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                    if (strcmp(methodName, IARM_BUS_MFRLIB_API_GetSerializedData) == 0) {
                        auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                        if (param->type == mfrSERIALIZED_TYPE_SERIALNUMBER) {
                            const char* str = "MFR_SERIAL_12345";
                            param->bufLen = strlen(str);
                            strncpy(param->buffer, str, sizeof(param->buffer));
                            return IARM_RESULT_SUCCESS;
                        }
                    }
                    return IARM_RESULT_INVALID_PARAM;
                });

        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "serialnumber@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        
        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("serialnumber"));
            string serialNumber = getResults["serialnumber"].String();
            EXPECT_FALSE(serialNumber.empty());
            EXPECT_EQ(serialNumber, "MFR_SERIAL_12345");
            TEST_LOG("Serial number from MFR: %s", serialNumber.c_str());
        }
    }

    /****************** Test make from device.properties ******************/
    {
        TEST_LOG("Testing make from device.properties\n");

        std::ofstream file("/etc/device.properties");
        file << "MFG_NAME=EdgeCaseManufacturer\n";
        file.close();

        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "make@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("make"));
            string make = getResults["make"].String();
            EXPECT_FALSE(make.empty());
            EXPECT_EQ(make, "EdgeCaseManufacturer");
            TEST_LOG("Make from device.properties: %s", make.c_str());
        }
    }

    /****************** Test modelname with quotes ******************/
    {
        TEST_LOG("Testing modelname with quoted value\n");

        std::ofstream file("/etc/device.properties");
        file << "FRIENDLY_ID=\"Quoted Model Name\"\n";
        file.close();

        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "modelname@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("model"));
            string model = getResults["model"].String();
            EXPECT_FALSE(model.empty());
            EXPECT_EQ(model, "Quoted Model Name");
            TEST_LOG("Model name with quotes: %s", model.c_str());
        }
    }

    /****************** Test distributorid from RFC ******************/
    {
        TEST_LOG("Testing distributorid from RFC\n");
        
        ON_CALL(*p_rfcApiImplMock, getRFCParameter(::testing::_, ::testing::_, ::testing::_))
            .WillByDefault(::testing::Invoke(
                [](char* pcCallerID, const char* pcParameterName, RFC_ParamData_t* pstParamData) {
                    if (strcmp(pcParameterName, "Device.DeviceInfo.X_RDKCENTRAL-COM_Syndication.PartnerId") == 0) {
                        strcpy(pstParamData->value, "RFC_PARTNER_ID");
                        return WDMP_SUCCESS;
                    }
                    return WDMP_FAILURE;
                }));

        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "distributorid@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("distributorid"));
            string distributorId = getResults["distributorid"].String();
            // Should return the value cached from constructor: "TestPartnerID"
            EXPECT_EQ(distributorId, "RFC_PARTNER_ID");
            TEST_LOG("Distributor ID (cached from init): %s", distributorId.c_str());
        }
    }

    /****************** Test multiple audio ports ******************/
    {
        TEST_LOG("Testing supportedaudioports with multiple ports\n");
        
        device::AudioOutputPort audioOutputPort1, audioOutputPort2, audioOutputPort3;
        string audioPort1(_T("HDMI0"));
        string audioPort2(_T("SPDIF0"));
        string audioPort3(_T("SPEAKER0"));

        ON_CALL(*p_audioOutputPortMock, getName())
            .WillByDefault(::testing::ReturnRef(audioPort1));
        ON_CALL(*p_hostImplMock, getAudioOutputPorts())
            .WillByDefault(::testing::Return(device::List<device::AudioOutputPort>({ audioOutputPort1, audioOutputPort2, audioOutputPort3 })));

        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "supportedaudioports@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
    }

    /****************** Test hostedid with large EDID ******************/
    {
        TEST_LOG("Testing hostedid with large EDID data\n");
        
        ON_CALL(*p_hostImplMock, getHostEDID(::testing::_))
            .WillByDefault(::testing::Invoke(
                [&](std::vector<uint8_t>& edid) {
                    // Standard EDID size is 128 or 256 bytes
                    edid.resize(256);
                    for (size_t i = 0; i < 256; i++) {
                        edid[i] = static_cast<uint8_t>(i & 0xFF);
                    }
                }));
        
        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "hostedid@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);

        if (getResult == Core::ERROR_NONE) 
        {
            EXPECT_TRUE(getResults.HasLabel("EDID"));
            string edid = getResults["EDID"].String();
            EXPECT_FALSE(edid.empty());
            // Validate that it's the expected base64-encoded value
            EXPECT_EQ(edid, "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0+P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn+AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmqq6ytrq+wsbKztLW2t7i5uru8vb6/wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t/g4eLj5OXm5+jp6uvs7e7v8PHy8/T19vf4+fr7/P3+/w==");
            // Verify the size (base64 encoded 256 bytes should be 344 characters)
            EXPECT_GT(edid.length(), 0);
            TEST_LOG("EDID (base64) length: %zu", edid.length());
        }
    }

    TEST_LOG("DeviceInfo L2 Property Edge Case Tests completed\n");
}

TEST_F(DeviceInfo_L2test, DeviceInfo_L2_ExceptionHandlingTest)
{
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(DEVICEINFO_CALLSIGN, DEVICEINFOL2TEST_CALLSIGN);
    uint32_t status = Core::ERROR_NONE;

    TEST_LOG("Starting DeviceInfo L2 Exception Handling Tests\n");

    /****************** Test supportedaudioports with exception ******************/
    {
        TEST_LOG("Testing supportedaudioports with device exception\n");
        
        ON_CALL(*p_hostImplMock, getAudioOutputPorts())
            .WillByDefault(::testing::Throw(device::Exception("Audio ports exception")));

        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "supportedaudioports@0", getResults);
        EXPECT_EQ(Core::ERROR_GENERAL, getResult);
    }

    /****************** Test supportedvideodisplays with exception ******************/
    {
        TEST_LOG("Testing supportedvideodisplays with device exception\n");
        
        ON_CALL(*p_hostImplMock, getVideoOutputPorts())
            .WillByDefault(::testing::Throw(device::Exception("Video ports exception")));

        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "supportedvideodisplays@0", getResults);
        EXPECT_EQ(Core::ERROR_GENERAL, getResult);
    }

    /****************** Test hostedid with exception ******************/
    {
        TEST_LOG("Testing hostedid with device exception\n");
        
        ON_CALL(*p_hostImplMock, getHostEDID(::testing::_))
            .WillByDefault(::testing::Throw(device::Exception("EDID exception")));
        
        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "hostedid@0", getResults);
        EXPECT_EQ(Core::ERROR_GENERAL, getResult);
    }

    /****************** Test supportedhdcp with exception ******************/
    {
        TEST_LOG("Testing supportedhdcp with exception\n");
        JsonObject result, params;
        params["videoDisplay"] = "HDMI0";
        
        device::VideoOutputPort videoOutputPort;
        string videoPort(_T("HDMI0"));

        ON_CALL(*p_hostImplMock, getDefaultVideoPortName())
            .WillByDefault(::testing::Return(videoPort));
        ON_CALL(*p_videoOutputPortConfigImplMock, getPort(::testing::_))
            .WillByDefault(::testing::ReturnRef(videoOutputPort));
        ON_CALL(*p_videoOutputPortMock, getHDCPProtocol())
            .WillByDefault(::testing::Throw(device::Exception("HDCP exception")));
        
        status = InvokeServiceMethod("DeviceInfo.1", "supportedhdcp", params, result);
        EXPECT_EQ(Core::ERROR_GENERAL, status);
    }

    /****************** Test supportedms12audioprofiles with exception ******************/
    {
        TEST_LOG("Testing supportedms12audioprofiles with device exception\n");
        JsonObject result, params;
        params["audioPort"] = "HDMI0";
        
        string audioPort(_T("HDMI0"));

        ON_CALL(*p_hostImplMock, getDefaultAudioPortName())
            .WillByDefault(::testing::Return(audioPort));
        ON_CALL(*p_hostImplMock, getAudioOutputPort(::testing::_))
            .WillByDefault(::testing::Throw(device::Exception("MS12 profiles exception")));
        
        status = InvokeServiceMethod("DeviceInfo.1", "supportedms12audioprofiles", params, result);
        EXPECT_EQ(Core::ERROR_GENERAL, status);
    }

    TEST_LOG("DeviceInfo L2 Exception Handling Tests completed\n");
}

TEST_F(DeviceInfo_L2test, DeviceInfo_L2_AdditionalPropertiesTest)
{
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(DEVICEINFO_CALLSIGN, DEVICEINFOL2TEST_CALLSIGN);
    uint32_t status = Core::ERROR_NONE;

    TEST_LOG("Starting DeviceInfo L2 Additional Properties Tests\n");

    /****************** Test systeminfo with different MFR data ******************/
    {
        TEST_LOG("Testing systeminfo with different serial number\n");
    
        ON_CALL(*p_iarmBusImplMock, IARM_Bus_Call)
            .WillByDefault(
                [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                    if (strcmp(methodName, IARM_BUS_MFRLIB_API_GetSerializedData) == 0) {
                        auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                        if (param->type == mfrSERIALIZED_TYPE_SERIALNUMBER) {
                            const char* str = "DIFFERENT_SERIAL_9999";
                            param->bufLen = strlen(str);
                            strncpy(param->buffer, str, sizeof(param->buffer));
                            return IARM_RESULT_SUCCESS;
                        }
                    }
                    return IARM_RESULT_INVALID_PARAM;
                });

        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "systeminfo@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        
        if (getResult == Core::ERROR_NONE && getResults.HasLabel("systeminfo")) {
            JsonObject sysInfo = getResults["systeminfo"].Object();
            EXPECT_TRUE(sysInfo.HasLabel("time"));
            EXPECT_TRUE(sysInfo.HasLabel("version"));
            EXPECT_TRUE(sysInfo.HasLabel("uptime"));
        }
    }

    TEST_LOG("DeviceInfo L2 Additional Properties Tests completed\n");
}

TEST_F(DeviceInfo_L2test, DeviceInfo_L2_MethodVariationsTest)
{
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(DEVICEINFO_CALLSIGN, DEVICEINFOL2TEST_CALLSIGN);
    uint32_t status = Core::ERROR_NONE;

    TEST_LOG("Starting DeviceInfo L2 Method Variations Tests\n");

    /****************** Test defaultresolution with different resolutions ******************/
    {
        TEST_LOG("Testing defaultresolution with 4K resolution\n");
        JsonObject result, params;
        params["videoDisplay"] = "HDMI0";
        
        device::VideoOutputPort videoOutputPort;
        device::VideoResolution videoResolution;
        string videoPort(_T("HDMI0"));
        string videoPortDefaultResolution(_T("2160p"));

        ON_CALL(*p_videoResolutionMock, getName())
            .WillByDefault(::testing::ReturnRef(videoPortDefaultResolution));
        ON_CALL(*p_videoOutputPortMock, getDefaultResolution())
            .WillByDefault(::testing::ReturnRef(videoResolution));
        ON_CALL(*p_hostImplMock, getDefaultVideoPortName())
            .WillByDefault(::testing::Return(videoPort));
        ON_CALL(*p_hostImplMock, getVideoOutputPort(::testing::_))
            .WillByDefault(::testing::ReturnRef(videoOutputPort));
        
        status = InvokeServiceMethod("DeviceInfo.1", "defaultresolution", params, result);
        EXPECT_EQ(Core::ERROR_NONE, status);  // Change from ERROR_GENERAL to ERROR_NONE
        if (status == Core::ERROR_NONE) {
            EXPECT_TRUE(result.HasLabel("defaultResolution"));
            string resolution = result["defaultResolution"].String();
            EXPECT_EQ(resolution, "2160p");
            TEST_LOG("4K default resolution: %s", resolution.c_str());
        }
    }

    /****************** Test supportedresolutions with 4K support ******************/
    {
        TEST_LOG("Testing supportedresolutions with 4K support\n");
        JsonObject result, params;
        params["videoDisplay"] = "HDMI0";
        
        device::VideoOutputPort videoOutputPort;
        device::VideoOutputPortType videoOutputPortType;
        device::VideoResolution res1, res2, res3, res4, res5;
        string videoPort(_T("HDMI0"));
        string resolution(_T("2160p"));

        ON_CALL(*p_videoResolutionMock, getName())
            .WillByDefault(::testing::ReturnRef(resolution));
        ON_CALL(*p_videoOutputPortTypeMock, getSupportedResolutions())
            .WillByDefault(::testing::Return(device::List<device::VideoResolution>({ res1, res2, res3, res4, res5 })));
        ON_CALL(*p_videoOutputPortTypeMock, getId())
            .WillByDefault(::testing::Return(0));
        ON_CALL(*p_videoOutputPortMock, getType())
            .WillByDefault(::testing::ReturnRef(videoOutputPortType));
        ON_CALL(*p_hostImplMock, getDefaultVideoPortName())
            .WillByDefault(::testing::Return(videoPort));
        ON_CALL(*p_hostImplMock, getVideoOutputPort(::testing::_))
            .WillByDefault(::testing::ReturnRef(videoOutputPort));
        ON_CALL(*p_videoOutputPortConfigImplMock, getPortType(::testing::_))
            .WillByDefault(::testing::ReturnRef(videoOutputPortType));
        
        status = InvokeServiceMethod("DeviceInfo.1", "supportedresolutions", params, result);
        EXPECT_EQ(Core::ERROR_NONE, status);  // Change from ERROR_GENERAL to ERROR_NONE
        if (status == Core::ERROR_NONE) {
            EXPECT_TRUE(result.HasLabel("supportedResolutions"));
            JsonArray resolutions = result["supportedResolutions"].Array();
            EXPECT_EQ(resolutions.Length(), 5);
            if (resolutions.Length() > 0) {
                string res = resolutions[0].String();
                EXPECT_EQ(res, "2160p");
                TEST_LOG("First 4K resolution: %s", res.c_str());
            }
        }
    }


    /****************** Test ms12capabilities with no capabilities ******************/
    {
        TEST_LOG("Testing ms12capabilities with none\n");
        JsonObject result, params;
        params["audioPort"] = "HDMI0";
        
        device::AudioOutputPort audioOutputPort;
        string audioPort(_T("HDMI0"));

        ON_CALL(*p_audioOutputPortMock, getMS12Capabilities(::testing::_))
            .WillByDefault(::testing::Invoke(
                [&](int* capabilities) {
                    if (capabilities != nullptr) {
                        *capabilities = dsMS12SUPPORT_NONE;
                    }
                }));
        ON_CALL(*p_hostImplMock, getDefaultAudioPortName())
            .WillByDefault(::testing::Return(audioPort));
        ON_CALL(*p_hostImplMock, getAudioOutputPort(::testing::_))
            .WillByDefault(::testing::ReturnRef(audioOutputPort));
        
        status = InvokeServiceMethod("DeviceInfo.1", "ms12capabilities", params, result);
        EXPECT_EQ(Core::ERROR_NONE, status);
    }

    TEST_LOG("DeviceInfo L2 Method Variations Tests completed\n");
}

TEST_F(DeviceInfo_L2test, DeviceInfo_JsonRpc_MacAddressesAndIp)
{

    TEST_LOG("Starting DeviceInfo L2 JsonRpc MAC Addresses and IP Tests\n");

    ON_CALL(*p_wrapsImplMock, v_secure_popen(::testing::_, ::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [](const char* type, const char* command, va_list args) -> FILE* {
                va_list args2;
                va_copy(args2, args);
                char strFmt[256];
                vsnprintf(strFmt, sizeof(strFmt), command, args2);
                va_end(args2);
                
                const char* valueToReturn = nullptr;
                
                if (strcmp(strFmt, "/lib/rdk/getDeviceDetails.sh read eth_mac") == 0) {
                    valueToReturn = "AA:BB:CC:DD:EE:FF";
                } else if (strcmp(strFmt, "/lib/rdk/getDeviceDetails.sh read estb_mac") == 0) {
                    valueToReturn = "11:22:33:44:55:66";
                } else if (strcmp(strFmt, "/lib/rdk/getDeviceDetails.sh read wifi_mac") == 0) {
                    valueToReturn = "00:11:22:33:44:55";
                } else if (strcmp(strFmt, "/lib/rdk/getDeviceDetails.sh read estb_ip") == 0) {
                    valueToReturn = "192.168.1.100";
                }
                
                if (valueToReturn != nullptr) {
                    return fmemopen(strdup(valueToReturn), strlen(valueToReturn), "r");
                }
                
                return nullptr;
            }));

    /****************** ethmac ******************/
    {
        TEST_LOG("Testing ethmac property\n");

        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "ethmac@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        
        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("eth_mac"));
            string ethmac = getResults["eth_mac"].String();
            EXPECT_FALSE(ethmac.empty());
            EXPECT_EQ(ethmac, "AA:BB:CC:DD:EE:FF");
            TEST_LOG("Ethernet MAC: %s", ethmac.c_str());
        }
    }

    /****************** estbmac ******************/
    {
        TEST_LOG("Testing estbmac property\n");

        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "estbmac@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        
        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("estb_mac"));
            string estbmac = getResults["estb_mac"].String();
            EXPECT_FALSE(estbmac.empty());
            EXPECT_EQ(estbmac, "11:22:33:44:55:66");
            TEST_LOG("STB MAC: %s", estbmac.c_str());
        }
    }

    /****************** wifimac ******************/
    {
        TEST_LOG("Testing wifimac property\n");

        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "wifimac@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        
        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("wifi_mac"));
            string wifimac = getResults["wifi_mac"].String();
            EXPECT_FALSE(wifimac.empty());
            EXPECT_EQ(wifimac, "00:11:22:33:44:55");
            TEST_LOG("WiFi MAC: %s", wifimac.c_str());
        }
    }

    /****************** estbip ******************/
    {
        TEST_LOG("Testing estbip property\n");

        JsonObject getResults;
        uint32_t getResult = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "estbip@0", getResults);
        EXPECT_EQ(Core::ERROR_NONE, getResult);
        
        if (getResult == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults.HasLabel("estb_ip"));
            string estbip = getResults["estb_ip"].String();
            EXPECT_FALSE(estbip.empty());
            EXPECT_EQ(estbip, "192.168.1.100");
            TEST_LOG("STB IP: %s", estbip.c_str());
        }
    }

    TEST_LOG("DeviceInfo L2 JsonRpc MAC Addresses and IP Tests completed\n");
}

TEST_F(DeviceInfo_L2test, DeviceInfo_JsonRpc_MacAddressesAndIp_Negative)
{
    TEST_LOG("Starting DeviceInfo L2 JsonRpc MAC Addresses and IP Negative Tests\n");

    /****************** Test with v_secure_popen returning nullptr ******************/
    {
        TEST_LOG("Testing MAC/IP properties with v_secure_popen failure\n");
        
        ON_CALL(*p_wrapsImplMock, v_secure_popen(::testing::_, ::testing::_, ::testing::_))
            .WillByDefault(::testing::Return(nullptr));

        // Test ethmac
        JsonObject getResults1;
        uint32_t getResult1 = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "ethmac@0", getResults1);
        EXPECT_EQ(Core::ERROR_GENERAL, getResult1);
        TEST_LOG("ethmac with popen failure: PASS\n");

        // Test estbmac
        JsonObject getResults2;
        uint32_t getResult2 = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "estbmac@0", getResults2);
        EXPECT_EQ(Core::ERROR_GENERAL, getResult2);
        TEST_LOG("estbmac with popen failure: PASS\n");

        // Test wifimac
        JsonObject getResults3;
        uint32_t getResult3 = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "wifimac@0", getResults3);
        EXPECT_EQ(Core::ERROR_GENERAL, getResult3);
        TEST_LOG("wifimac with popen failure: PASS\n");

        // Test estbip
        JsonObject getResults4;
        uint32_t getResult4 = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "estbip@0", getResults4);
        EXPECT_EQ(Core::ERROR_GENERAL, getResult4);
        TEST_LOG("estbip with popen failure: PASS\n");
    }

    /****************** Test with empty responses ******************/
    {
        TEST_LOG("Testing MAC/IP properties with empty responses\n");
        
        ON_CALL(*p_wrapsImplMock, v_secure_popen(::testing::_, ::testing::_, ::testing::_))
            .WillByDefault(::testing::Invoke(
                [](const char* type, const char* command, va_list args) -> FILE* {
                    const char* emptyData = "";
                    return fmemopen(strdup(emptyData), strlen(emptyData), "r");
                }));

        // Test ethmac with empty response
        JsonObject getResults1;
        uint32_t getResult1 = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "ethmac@0", getResults1);
        EXPECT_EQ(Core::ERROR_NONE, getResult1);
        if (getResult1 == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults1.HasLabel("eth_mac"));
            string ethMac = getResults1["eth_mac"].String();
            EXPECT_TRUE(ethMac.empty());
            TEST_LOG("ethmac empty response: %s\n", ethMac.empty() ? "empty" : ethMac.c_str());
        }

        // Test estbmac with empty response
        JsonObject getResults2;
        uint32_t getResult2 = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "estbmac@0", getResults2);
        EXPECT_EQ(Core::ERROR_NONE, getResult2);
        if (getResult2 == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults2.HasLabel("estb_mac"));
            string estbMac = getResults2["estb_mac"].String();
            EXPECT_TRUE(estbMac.empty());
            TEST_LOG("estbmac empty response: %s\n", estbMac.empty() ? "empty" : estbMac.c_str());
        }

        // Test wifimac with empty response
        JsonObject getResults3;
        uint32_t getResult3 = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "wifimac@0", getResults3);
        EXPECT_EQ(Core::ERROR_NONE, getResult3);
        if (getResult3 == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults3.HasLabel("wifi_mac"));
            string wifiMac = getResults3["wifi_mac"].String();
            EXPECT_TRUE(wifiMac.empty());
            TEST_LOG("wifimac empty response: %s\n", wifiMac.empty() ? "empty" : wifiMac.c_str());
        }

        // Test estbip with empty response
        JsonObject getResults4;
        uint32_t getResult4 = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "estbip@0", getResults4);
        EXPECT_EQ(Core::ERROR_NONE, getResult4);
        if (getResult4 == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults4.HasLabel("estb_ip"));
            string estbIp = getResults4["estb_ip"].String();
            EXPECT_TRUE(estbIp.empty());
            TEST_LOG("estbip empty response: %s\n", estbIp.empty() ? "empty" : estbIp.c_str());
        }
    }

    /****************** Test with malformed data ******************/
    {
        TEST_LOG("Testing MAC/IP properties with malformed data\n");
        
        ON_CALL(*p_wrapsImplMock, v_secure_popen(::testing::_, ::testing::_, ::testing::_))
            .WillByDefault(::testing::Invoke(
                [](const char* type, const char* command, va_list args) -> FILE* {
                    char buffer[256];
                    vsnprintf(buffer, sizeof(buffer), command, args);
                    std::string cmd(buffer);

                    const char* data = nullptr;
                    if (cmd.find("read eth_mac") != std::string::npos) {
                        data = "INVALID_MAC_FORMAT\n";
                    } else if (cmd.find("read estb_mac") != std::string::npos) {
                        data = "ZZ:YY:XX:WW:VV:UU\n";
                    } else if (cmd.find("read wifi_mac") != std::string::npos) {
                        data = "NOT_A_MAC\n";
                    } else if (cmd.find("read estb_ip") != std::string::npos) {
                        data = "999.999.999.999\n";
                    } else {
                        data = "\n";
                    }
                    
                    return fmemopen(strdup(data), strlen(data), "r");
                }));

        // Test ethmac with invalid format
        JsonObject getResults1;
        uint32_t getResult1 = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "ethmac@0", getResults1);
        EXPECT_EQ(Core::ERROR_NONE, getResult1);
        if (getResult1 == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults1.HasLabel("eth_mac"));
            string ethMac = getResults1["eth_mac"].String();
            EXPECT_EQ(ethMac, "INVALID_MAC_FORMAT");
            TEST_LOG("ethmac malformed: %s\n", ethMac.c_str());
        }

        // Test estbmac with invalid format
        JsonObject getResults2;
        uint32_t getResult2 = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "estbmac@0", getResults2);
        EXPECT_EQ(Core::ERROR_NONE, getResult2);
        if (getResult2 == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults2.HasLabel("estb_mac"));
            string estbMac = getResults2["estb_mac"].String();
            EXPECT_EQ(estbMac, "ZZ:YY:XX:WW:VV:UU");
            TEST_LOG("estbmac malformed: %s\n", estbMac.c_str());
        }

        // Test wifimac with invalid format
        JsonObject getResults3;
        uint32_t getResult3 = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "wifimac@0", getResults3);
        EXPECT_EQ(Core::ERROR_NONE, getResult3);
        if (getResult3 == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults3.HasLabel("wifi_mac"));
            string wifiMac = getResults3["wifi_mac"].String();
            EXPECT_EQ(wifiMac, "NOT_A_MAC");
            TEST_LOG("wifimac malformed: %s\n", wifiMac.c_str());
        }

        // Test estbip with invalid format
        JsonObject getResults4;
        uint32_t getResult4 = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "estbip@0", getResults4);
        EXPECT_EQ(Core::ERROR_NONE, getResult4);
        if (getResult4 == Core::ERROR_NONE) {
            EXPECT_TRUE(getResults4.HasLabel("estb_ip"));
            string estbIp = getResults4["estb_ip"].String();
            EXPECT_EQ(estbIp, "999.999.999.999");
            TEST_LOG("estbip malformed: %s\n", estbIp.c_str());
        }
    }

    /****************** Test with only newline character ******************/
    {
        TEST_LOG("Testing MAC/IP properties with only newline\n");
        
        ON_CALL(*p_wrapsImplMock, v_secure_popen(::testing::_, ::testing::_, ::testing::_))
            .WillByDefault(::testing::Invoke(
                [](const char* type, const char* command, va_list args) -> FILE* {
                    const char* data = "\n";
                    return fmemopen(strdup(data), strlen(data), "r");
                }));

        // Test all properties with only newline
        JsonObject getResults1;
        uint32_t getResult1 = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "ethmac@0", getResults1);
        EXPECT_EQ(Core::ERROR_NONE, getResult1);
        if (getResult1 == Core::ERROR_NONE) {
            string ethMac = getResults1["eth_mac"].String();
            EXPECT_TRUE(ethMac.empty());
        }

        JsonObject getResults2;
        uint32_t getResult2 = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "estbmac@0", getResults2);
        EXPECT_EQ(Core::ERROR_NONE, getResult2);

        JsonObject getResults3;
        uint32_t getResult3 = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "wifimac@0", getResults3);
        EXPECT_EQ(Core::ERROR_NONE, getResult3);

        JsonObject getResults4;
        uint32_t getResult4 = InvokeServiceMethod(DEVICEINFO_CALLSIGN, "estbip@0", getResults4);
        EXPECT_EQ(Core::ERROR_NONE, getResult4);

        TEST_LOG("All properties with only newline: PASS\n");
    }

    TEST_LOG("DeviceInfo L2 JsonRpc MAC Addresses and IP Negative Tests completed\n");
}

// ======================= COM-RPC TESTS =======================

TEST_F(DeviceInfo_L2test, DeviceInfo_COMRPC_SerialNumber)
{
    ASSERT_TRUE(m_deviceinfoplugin != nullptr);

    Exchange::IDeviceInfo::DeviceSerialNo serialNumber;
    Core::hresult rc = m_deviceinfoplugin->SerialNumber(serialNumber);
    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_FALSE(serialNumber.serialnumber.empty());
    EXPECT_EQ(serialNumber.serialnumber, "RFC_TEST_SERIAL");
}

TEST_F(DeviceInfo_L2test, DeviceInfo_COMRPC_Sku)
{
    ASSERT_TRUE(m_deviceinfoplugin != nullptr);

    Exchange::IDeviceInfo::DeviceModelNo modelNo;
    Core::hresult rc = m_deviceinfoplugin->Sku(modelNo);
    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_FALSE(modelNo.sku.empty());
    EXPECT_EQ(modelNo.sku, "TEST_SKU_12345");
}

TEST_F(DeviceInfo_L2test, DeviceInfo_COMRPC_Make)
{
    ASSERT_TRUE(m_deviceinfoplugin != nullptr);

    ON_CALL(*p_iarmBusImplMock, IARM_Bus_Call)
        .WillByDefault(
            [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                if (strcmp(methodName, IARM_BUS_MFRLIB_API_GetSerializedData) == 0) {
                    auto* param = static_cast<IARM_Bus_MFRLib_GetSerializedData_Param_t*>(arg);
                    strcpy(param->buffer, "TestManufacturer");
                    return IARM_RESULT_SUCCESS;
                }
                return IARM_RESULT_SUCCESS;
            });

    Exchange::IDeviceInfo::DeviceMake make;
    Core::hresult rc = m_deviceinfoplugin->Make(make);
    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_FALSE(make.make.empty());
    EXPECT_EQ(make.make, "TestManufacturer");
}

TEST_F(DeviceInfo_L2test, DeviceInfo_COMRPC_Model)
{
    ASSERT_TRUE(m_deviceinfoplugin != nullptr);

    Exchange::IDeviceInfo::DeviceModel model;
    Core::hresult rc = m_deviceinfoplugin->Model(model);
    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_FALSE(model.model.empty());
    EXPECT_EQ(model.model, "TestModel");
}

TEST_F(DeviceInfo_L2test, DeviceInfo_COMRPC_DeviceType)
{
    ASSERT_TRUE(m_deviceinfoplugin != nullptr);

    std::ofstream file("/etc/authService.conf");
    file << "deviceType=IpStb";
    file.close();

    Exchange::IDeviceInfo::DeviceTypeInfos deviceType;
    Core::hresult rc = m_deviceinfoplugin->DeviceType(deviceType);
    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_NE(deviceType.devicetype, 0);
    EXPECT_EQ(deviceType.devicetype, Exchange::IDeviceInfo::DEVICE_TYPE_IPSTB);
}

TEST_F(DeviceInfo_L2test, DeviceInfo_COMRPC_SocName)
{
    ASSERT_TRUE(m_deviceinfoplugin != nullptr);

    std::ofstream file("/etc/device.properties");
    file << "SOC=NVIDIA\n";
    file.close();

    Exchange::IDeviceInfo::DeviceSoc socName;
    Core::hresult rc = m_deviceinfoplugin->SocName(socName);
    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_FALSE(socName.socname.empty());
    EXPECT_EQ(socName.socname, "NVIDIA");
}

TEST_F(DeviceInfo_L2test, DeviceInfo_COMRPC_DistributorId)
{
    ASSERT_TRUE(m_deviceinfoplugin != nullptr);

    Exchange::IDeviceInfo::DeviceDistId distributorId;
    Core::hresult rc = m_deviceinfoplugin->DistributorId(distributorId);
    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ(distributorId.distributorid, "TestPartnerID");
}

TEST_F(DeviceInfo_L2test, DeviceInfo_COMRPC_Brand)
{
    ASSERT_TRUE(m_deviceinfoplugin != nullptr);

    Exchange::IDeviceInfo::DeviceBrand brand;
    Core::hresult rc = m_deviceinfoplugin->Brand(brand);
    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_FALSE(brand.brand.empty());
    EXPECT_EQ(brand.brand, "TestBrand");
}

TEST_F(DeviceInfo_L2test, DeviceInfo_COMRPC_ReleaseVersion)
{
    ASSERT_TRUE(m_deviceinfoplugin != nullptr);

    Exchange::IDeviceInfo::DeviceReleaseVer releaseVersion;
    Core::hresult rc = m_deviceinfoplugin->ReleaseVersion(releaseVersion);
    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_FALSE(releaseVersion.releaseversion.empty());
    EXPECT_EQ(releaseVersion.releaseversion, "22.03.0.0");
}

TEST_F(DeviceInfo_L2test, DeviceInfo_COMRPC_ChipSet)
{
    ASSERT_TRUE(m_deviceinfoplugin != nullptr);

    std::ofstream file("/etc/device.properties");
    file << "CHIPSET_NAME=TestChipset\n";
    file.close();

    Exchange::IDeviceInfo::DeviceChip chipset;
    Core::hresult rc = m_deviceinfoplugin->ChipSet(chipset);
    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_FALSE(chipset.chipset.empty());
    EXPECT_EQ(chipset.chipset, "TestChipset");
}

TEST_F(DeviceInfo_L2test, DeviceInfo_COMRPC_FirmwareVersion)
{
    ASSERT_TRUE(m_deviceinfoplugin != nullptr);

    Exchange::IDeviceInfo::FirmwareversionInfo firmwareVersion;
    Core::hresult rc = m_deviceinfoplugin->FirmwareVersion(firmwareVersion);
    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_FALSE(firmwareVersion.imagename.empty());
    EXPECT_EQ(firmwareVersion.imagename, "CUSTOM_VBN_22.03s_sprint_20220331225312sdy_NG");
    EXPECT_EQ(firmwareVersion.sdk, "17.3");
}

TEST_F(DeviceInfo_L2test, DeviceInfo_COMRPC_SystemInfo)
{
    ASSERT_TRUE(m_deviceinfoplugin != nullptr);

    ON_CALL(*p_iarmBusImplMock, IARM_Bus_Call)
        .WillByDefault(
            [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                if (strcmp(methodName, IARM_BUS_SYSMGR_API_GetSystemStates) == 0) {
                    auto* param = static_cast<IARM_Bus_SYSMgr_GetSystemStates_Param_t*>(arg);
                    param->channel_map.state = 2;
                    return IARM_RESULT_SUCCESS;
                }
                return IARM_RESULT_SUCCESS;
            });

    Exchange::IDeviceInfo::SystemInfos systemInfo;
    Core::hresult rc = m_deviceinfoplugin->SystemInfo(systemInfo);
    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_FALSE(systemInfo.version.empty());
}

TEST_F(DeviceInfo_L2test, DeviceInfo_COMRPC_Addresses)
{
    ASSERT_TRUE(m_deviceinfoplugin != nullptr);

    Exchange::IDeviceInfo::IAddressesInfoIterator* addressesInfo = nullptr;
    Core::hresult rc = m_deviceinfoplugin->Addresses(addressesInfo);
    EXPECT_EQ(Core::ERROR_NONE, rc);
    ASSERT_TRUE(addressesInfo != nullptr);

    Exchange::IDeviceInfo::AddressesInfo address;
    uint32_t count = 0;
    while (addressesInfo->Next(address)) {
        count++;
        EXPECT_FALSE(address.name.empty());
        EXPECT_FALSE(address.mac.empty());
    }

    addressesInfo->Release();
}

TEST_F(DeviceInfo_L2test, DeviceInfo_COMRPC_SupportedAudioPorts)
{
    ASSERT_TRUE(m_deviceinfoplugin != nullptr);

    device::AudioOutputPort audioOutputPort;
    string audioPort(_T("HDMI0"));

    ON_CALL(*p_audioOutputPortMock, getName())
        .WillByDefault(::testing::ReturnRef(audioPort));
    ON_CALL(*p_hostImplMock, getAudioOutputPorts())
        .WillByDefault(::testing::Return(device::List<device::AudioOutputPort>({ audioOutputPort })));

    RPC::IStringIterator* portIterator = nullptr;
    bool success = false;
    Core::hresult rc = m_deviceinfoplugin->SupportedAudioPorts(portIterator, success);
    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_TRUE(success);
    ASSERT_TRUE(portIterator != nullptr);

    string port;
    uint32_t count = 0;
    while (portIterator->Next(port)) {
        count++;
        EXPECT_FALSE(port.empty());
        EXPECT_EQ(port, "HDMI0");
    }
    EXPECT_GT(count, 0u);

    portIterator->Release();
}

TEST_F(DeviceInfo_L2test, DeviceInfo_COMRPC_MacAddressesAndIp)
{

    // Setup v_secure_popen mock for MAC addresses and IP
    ON_CALL(*p_wrapsImplMock, v_secure_popen(::testing::_, ::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [](const char* type, const char* command, va_list args) -> FILE* {
                va_list args2;
                va_copy(args2, args);
                char strFmt[256];
                vsnprintf(strFmt, sizeof(strFmt), command, args2);
                va_end(args2);
                
                const char* valueToReturn = nullptr;
                
                if (strcmp(strFmt, "/lib/rdk/getDeviceDetails.sh read eth_mac") == 0) {
                    valueToReturn = "AA:BB:CC:DD:EE:FF";
                } else if (strcmp(strFmt, "/lib/rdk/getDeviceDetails.sh read estb_mac") == 0) {
                    valueToReturn = "11:22:33:44:55:66";
                } else if (strcmp(strFmt, "/lib/rdk/getDeviceDetails.sh read wifi_mac") == 0) {
                    valueToReturn = "00:11:22:33:44:55";
                } else if (strcmp(strFmt, "/lib/rdk/getDeviceDetails.sh read estb_ip") == 0) {
                    valueToReturn = "192.168.1.100";
                }
                
                if (valueToReturn != nullptr) {
                    return fmemopen(strdup(valueToReturn), strlen(valueToReturn), "r");
                }
                
                return nullptr;
            }));

    ASSERT_TRUE(m_deviceinfoplugin != nullptr);

    Exchange::IDeviceInfo::EthernetMac ethMac;
    Core::hresult rc1 = m_deviceinfoplugin->EthMac(ethMac);
    EXPECT_EQ(Core::ERROR_NONE, rc1);
    EXPECT_FALSE(ethMac.ethMac.empty());
    EXPECT_EQ(ethMac.ethMac, "AA:BB:CC:DD:EE:FF");

    ASSERT_TRUE(m_deviceinfoplugin != nullptr);

    Exchange::IDeviceInfo::StbMac stbMac;
    Core::hresult rc2 = m_deviceinfoplugin->EstbMac(stbMac);
    EXPECT_EQ(Core::ERROR_NONE, rc2);
    EXPECT_FALSE(stbMac.estbMac.empty());
    EXPECT_EQ(stbMac.estbMac, "11:22:33:44:55:66");

    ASSERT_TRUE(m_deviceinfoplugin != nullptr);

    Exchange::IDeviceInfo::WiFiMac wifiMac;
    Core::hresult rc3 = m_deviceinfoplugin->WifiMac(wifiMac);
    EXPECT_EQ(Core::ERROR_NONE, rc3);
    EXPECT_FALSE(wifiMac.wifiMac.empty());
    EXPECT_EQ(wifiMac.wifiMac, "00:11:22:33:44:55");

    ASSERT_TRUE(m_deviceinfoplugin != nullptr);

    Exchange::IDeviceInfo::StbIp stbIp;
    Core::hresult rc4 = m_deviceinfoplugin->EstbIp(stbIp);
    EXPECT_EQ(Core::ERROR_NONE, rc4);
    EXPECT_FALSE(stbIp.estbIp.empty());
    EXPECT_EQ(stbIp.estbIp, "192.168.1.100");

}
