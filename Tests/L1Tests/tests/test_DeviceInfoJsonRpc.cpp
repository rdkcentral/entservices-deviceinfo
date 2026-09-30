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

#include "IarmBusMock.h"
#include "ServiceMock.h"
#include "COMLinkMock.h"
#include "DeviceSettingsMock.h"

#include "SystemInfo.h"

#include <condition_variable>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "ThunderPortability.h"

using namespace WPEFramework;

using ::testing::NiceMock;

namespace {
const string webPrefix = _T("/Service/DeviceInfo");
}

class DeviceInfoJsonRpcTest : public ::testing::Test {
protected:
    Core::ProxyType<Plugin::DeviceInfo> plugin;
    Core::JSONRPC::Handler& handler;
    DECL_CORE_JSONRPC_CONX connection;
    string response;

    DeviceInfoJsonRpcTest()
        : plugin(Core::ProxyType<Plugin::DeviceInfo>::Create())
        , handler(*plugin)
        , INIT_CONX(1, 0)
    {
    }
    virtual ~DeviceInfoJsonRpcTest() = default;
};

class DeviceInfoJsonRpcInitializedTest : public DeviceInfoJsonRpcTest {
protected:
    IarmBusImplMock   *p_iarmBusImplMock = nullptr ;
    NiceMock<ServiceMock> service;
    NiceMock<COMLinkMock> comLinkMock;
    Core::Sink<NiceMock<SystemInfo>> subSystem;
    std::mutex deviceSettingsMutex;
    std::condition_variable deviceSettingsCondition;
    bool deviceSettingsActivated = false;

    DeviceInfoJsonRpcInitializedTest()
        : DeviceInfoJsonRpcTest()
    {
        p_iarmBusImplMock  = new NiceMock <IarmBusImplMock>;
        IarmBus::setImpl(p_iarmBusImplMock);

        ON_CALL(service, ConfigLine())
            .WillByDefault(::testing::Return("{\"root\":{\"mode\":\"Off\"}}"));
        ON_CALL(service, WebPrefix())
            .WillByDefault(::testing::Return(webPrefix));
        ON_CALL(service, SubSystems())
            .WillByDefault(::testing::Invoke(
                [&]() {
                    PluginHost::ISubSystem* result = (&subSystem);
                    result->AddRef();
                    return result;
                }));
        ON_CALL(service, COMLink())
            .WillByDefault(::testing::Return(&comLinkMock));

        // DSHelper opens a COM-RPC link to DeviceSettings: the SmartInterface's
        // RegisterJob calls service.Register() then, on activation, resolves the
        // root IDeviceSettings via the shell's QueryInterface(IDeviceSettings::ID).
        // Needed even for the non-DeviceSettings tests below (systeminfo, addresses,
        // etc.) because DeviceAudioCapabilities::Configure()/DeviceVideoCapabilities::
        // Configure() both call DSHelper::Open() during Plugin::DeviceInfo::Initialize()
        // regardless of which JSON-RPC method a given test actually exercises.
        ON_CALL(service, QueryInterface(::testing::_))
            .WillByDefault(::testing::Invoke([](const uint32_t interfaceId) -> void* {
                if (interfaceId == Exchange::IDeviceSettings::ID) {
                    Exchange::IDeviceSettings* root = DeviceSettingsMock::Get();
                    root->AddRef();
                    return root;
                }
                return nullptr;
            }));
        ON_CALL(service, Register(::testing::Matcher<PluginHost::IPlugin::INotification*>(::testing::_)))
            .WillByDefault(::testing::Invoke(
                [this](PluginHost::IPlugin::INotification* notification) {
                    notification->Activated("org.rdk.DeviceSettings", &service);
                    {
                        std::lock_guard<std::mutex> lock(deviceSettingsMutex);
                        deviceSettingsActivated = true;
                    }
                    deviceSettingsCondition.notify_one();
                }));

        (void)DeviceSettingsMock::Get();

        EXPECT_EQ(string(""), plugin->Initialize(&service));

        {
            std::unique_lock<std::mutex> lock(deviceSettingsMutex);
            deviceSettingsCondition.wait_for(
                lock, std::chrono::seconds(5), [this]() { return deviceSettingsActivated; });
        }

        // Bound-delay for the async OnDeviceSettingsActivated() job (see prompt.md).
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    virtual ~DeviceInfoJsonRpcInitializedTest() override
    {
        plugin->Deinitialize(&service);

        // Cascades to every DeviceSettings<Component>Mock::Delete() (see
        // DeviceSettingsMock::Delete()).
        DeviceSettingsMock::Delete();

        IarmBus::setImpl(nullptr);
        if (p_iarmBusImplMock != nullptr)
        {
            delete p_iarmBusImplMock;
            p_iarmBusImplMock = nullptr;
        }
    }
};

// Stubs DeviceSettingsMock/DeviceSettingsAudioMock/DeviceSettingsVideoPortMock/
// DeviceSettingsHostMock directly (the COM-RPC interfaces DeviceInfoImplementation/
// DeviceAudioCapabilities/DeviceVideoCapabilities now call), instead of the legacy
// device::Host/AudioOutputPort/VideoOutputPort* HAL mocks. Default config: a single
// HDMI0 port for both audio and video, with a 1080p default/supported resolution —
// covers every test below; individual tests only add sub-interface (Audio/VideoPort/
// Host) stubs for the specific COM-RPC call they exercise.
class DeviceInfoJsonRpcInitializedDsTest : public DeviceInfoJsonRpcInitializedTest {
protected:
    DeviceInfoJsonRpcInitializedDsTest()
        : DeviceInfoJsonRpcInitializedTest()
    {
        SetDeviceConfig();

        ON_CALL(DeviceSettingsAudioMock::Mock(), GetAudioPort(::testing::_, ::testing::_, ::testing::_))
            .WillByDefault(::testing::Invoke(
                [](const Exchange::IDeviceSettingsAudio::AudioPortType type, const int32_t index, int32_t& handle) -> Core::hresult {
                    handle = static_cast<int32_t>(type) * 100 + index;
                    return Core::ERROR_NONE;
                }));
        ON_CALL(DeviceSettingsVideoPortMock::Mock(), GetVideoPort(::testing::_, ::testing::_, ::testing::_))
            .WillByDefault(::testing::Invoke(
                [](const Exchange::IDeviceSettingsVideoPort::VideoPort type, const int32_t index, int32_t& handle) -> Core::hresult {
                    handle = static_cast<int32_t>(type) * 100 + index;
                    return Core::ERROR_NONE;
                }));
    }
    virtual ~DeviceInfoJsonRpcInitializedDsTest() override = default;

    // (Re)configures the mocked DeviceSettings root's GetDeviceSettingConfigs()
    // response. Safe to call again from inside a TEST_F body before the first
    // JSON-RPC call in that test (config is loaded lazily, once, per test).
    void SetDeviceConfig(const string& defaultResolution = "1080p", const string& supportedResolutionNames = "1080p")
    {
        ON_CALL(DeviceSettingsMock::Mock(), GetDeviceSettingConfigs(::testing::_))
            .WillByDefault(::testing::Invoke(
                [defaultResolution, supportedResolutionNames](Exchange::IDeviceSettings::DeviceSettingConfigs& configs) -> Core::hresult {
                    Exchange::IDeviceSettings::AudioPortConfigInfo audioPortCfg{};
                    audioPortCfg.audioPortType = Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_HDMI;
                    audioPortCfg.audioPortIndex = 0;
                    configs.audioPorts.push_back(audioPortCfg);

                    Exchange::IDeviceSettings::VideoPortTypeConfig typeCfg{};
                    typeCfg.typeId = Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI;
                    typeCfg.dtcpSupported = false;
                    typeCfg.hdcpSupported = true;
                    typeCfg.restrictedResolution = 0;
                    typeCfg.supportedResolutionNames = supportedResolutionNames;
                    configs.videoPortTypes.push_back(typeCfg);

                    Exchange::IDeviceSettings::VideoPortPortConfig portCfg{};
                    portCfg.videoPortType = Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI;
                    portCfg.videoPortIndex = 0;
                    portCfg.defaultResolution = defaultResolution;
                    configs.videoPorts.push_back(portCfg);

                    return Core::ERROR_NONE;
                }));
    }
};

// No longer needs any mocks beyond DeviceInfoJsonRpcInitializedDsTest's — kept as a
// distinct type purely for test-grouping continuity with the pre-migration file.
class DeviceInfoJsonRpcInitializedDsVideoOutputTest : public DeviceInfoJsonRpcInitializedDsTest {
};

TEST_F(DeviceInfoJsonRpcTest, registeredMethods)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("socketinfo")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("addresses")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("systeminfo")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("firmwareversion")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("serialnumber")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("modelid")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("make")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("modelname")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("devicetype")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("distributorid")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("supportedaudioports")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("supportedvideodisplays")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("hostedid")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("defaultresolution")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("supportedresolutions")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("supportedhdcp")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("audiocapabilities")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("ms12capabilities")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("supportedms12audioprofiles")));
}


TEST_F(DeviceInfoJsonRpcInitializedTest, systeminfo)
{
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

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("systeminfo"), _T(""), response));
    EXPECT_THAT(response, ::testing::MatchesRegex("\\{"
                                                  "\"version\":\"#\","
                                                  "\"uptime\":[0-9]+,"
                                                  "\"totalram\":[0-9]+,"
                                                  "\"freeram\":[0-9]+,"
                                                  "\"totalswap\":[0-9]+,"
                                                  "\"freeswap\":[0-9]+,"
                                                  "\"devicename\":\".+\","
                                                  "\"cpuload\":\"[0-9]+\","
                                                  "\"cpuloadavg\":"
                                                  "\\{"
                                                  "\"avg1min\":[0-9]+,"
                                                  "\"avg5min\":[0-9]+,"
                                                  "\"avg15min\":[0-9]+"
                                                  "\\},"
                                                  "\"serialnumber\":\".+\","
                                                  "\"time\":\".+\""
                                                  "\\}"));
}


TEST_F(DeviceInfoJsonRpcInitializedTest, addresses)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("addresses"), _T(""), response));
    EXPECT_THAT(response, ::testing::MatchesRegex("\\["
                                                  "(\\{"
                                                  "\"name\":\"[^\"]+\","
                                                  "\"mac\":\"[^\"]+\""
                                                  "(,\"ip\":\\[(\"[^\"]+\",{0,1}){1,}\\]){0,1}"
                                                  "\\},{0,1}){0,}"
                                                  "\\]"));
}

TEST_F(DeviceInfoJsonRpcInitializedTest, socketinfo)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("socketinfo"), _T(""), response));
    EXPECT_THAT(response, ::testing::MatchesRegex("\\{\"runs\":[0-9]+\\}"));
}

TEST_F(DeviceInfoJsonRpcInitializedTest, firmwareversion)
{
    std::ofstream file("/version.txt");
    file << "imagename:CUSTOM5_VBN_2203_sprint_20220331225312sdy_NG\nSDK_VERSION=17.3\nMEDIARITE=8.3.53\nYOCTO_VERSION=dunfell\n";
    file.close();

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("firmwareversion"), _T(""), response));
    EXPECT_EQ(response, _T("{\"imagename\":\"CUSTOM5_VBN_2203_sprint_20220331225312sdy_NG\",\"rdk\":\"0.0\",\"sdk\":\"17.3\",\"mediarite\":\"8.3.53\",\"yocto\":\"dunfell\"}"));
}

TEST_F(DeviceInfoJsonRpcInitializedTest, DISABLED_make)
{
    std::ofstream file("/etc/device.properties");
    file << "MFG_NAME=CUSTOM4\nFRIENDLY_ID=\"CUSTOM4 CUSTOM9\"\n";
    file.close();

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("make"), _T(""), response));
    EXPECT_EQ(response, _T("{\"make\":\"pace\"}"));
}

TEST_F(DeviceInfoJsonRpcInitializedTest, modelname)
{
    std::ofstream file("/etc/device.properties");
    file << "MFG_NAME=CUSTOM4\nFRIENDLY_ID=\"CUSTOM4 CUSTOM9\"\n";
    file.close();

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("modelname"), _T(""), response));
    EXPECT_EQ(response, _T("{\"model\":\"CUSTOM4 CUSTOM9\"}"));
}

TEST_F(DeviceInfoJsonRpcInitializedTest, devicetype)
{
    std::ofstream file("/etc/authService.conf");
    file << "deviceType=IpStb";
    file.close();

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("devicetype"), _T(""), response));
    EXPECT_EQ(response, _T("{\"devicetype\":\"IpStb\"}"));
}

TEST_F(DeviceInfoJsonRpcInitializedDsTest, supportedaudioports)
{
    // Default config (fixture) already has a single HDMI0 audio port.
    // NOTE: SupportedAudioPorts() also declares a "success" @out param (see
    // IDeviceInfo.h) that this test's literal previously omitted.
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedaudioports"), _T(""), response));
    EXPECT_EQ(response, _T("{\"supportedAudioPorts\":[\"HDMI0\"],\"success\":true}"));
}

TEST_F(DeviceInfoJsonRpcInitializedDsTest, supportedvideodisplays)
{
    // Default config (fixture) already has a single HDMI0 video port.
    // NOTE: SupportedVideoDisplays() also declares a "success" @out param (see
    // IDeviceInfo.h) that this test's literal previously omitted.
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedvideodisplays"), _T(""), response));
    EXPECT_EQ(response, _T("{\"supportedVideoDisplays\":[\"HDMI0\"],\"success\":true}"));
}

TEST_F(DeviceInfoJsonRpcInitializedDsTest, hostedid)
{
    ON_CALL(DeviceSettingsHostMock::Mock(), GetEDID(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [](uint8_t edId[], const uint16_t) -> Core::hresult {
                edId[0] = 't'; edId[1] = 'e'; edId[2] = 's'; edId[3] = 't';
                return Core::ERROR_NONE;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("hostedid"), _T(""), response));
    EXPECT_EQ(response, _T("{\"EDID\":\"dGVzdA==\"}"));
}

TEST_F(DeviceInfoJsonRpcInitializedDsTest, defaultresolution)
{
    // Default config (fixture) already has HDMI0 with defaultResolution "1080p".
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T(""), response));
    EXPECT_EQ(response, _T("{\"defaultResolution\":\"1080p\"}"));
}

TEST_F(DeviceInfoJsonRpcInitializedDsVideoOutputTest, supportedresolutions)
{
    // Default config (fixture) already has HDMI0's type supporting "1080p".
    // NOTE: SupportedResolutions() also declares a "success" @out param (see
    // IDeviceInfo.h) that this test's literal previously omitted.
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedresolutions"), _T(""), response));
    EXPECT_EQ(response, _T("{\"supportedResolutions\":[\"1080p\"],\"success\":true}"));
}

TEST_F(DeviceInfoJsonRpcInitializedDsVideoOutputTest, supportedhdcp)
{
    ON_CALL(DeviceSettingsVideoPortMock::Mock(), GetHDCPProtocolVersionOnVideoPort(::testing::_, ::testing::_))
        .WillByDefault(::testing::DoAll(
            ::testing::SetArgReferee<1>(Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_2X),
            ::testing::Return(Core::ERROR_NONE)));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T(""), response));
    EXPECT_EQ(response, _T("{\"supportedHDCPVersion\":\"2.2\"}"));
}

TEST_F(DeviceInfoJsonRpcInitializedDsTest, audiocapabilities)
{
    ON_CALL(DeviceSettingsAudioMock::Mock(), GetAudioCapabilities(::testing::_, ::testing::_))
        .WillByDefault(::testing::DoAll(
            ::testing::SetArgReferee<1>(Exchange::IDeviceSettingsAudio::AUDIO_CAPS_ATMOS |
                                         Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DOLBY_DIGITAL |
                                         Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DOLBY_DIGITAL_PLUS |
                                         Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DIGITAL_AUDIO_DELIVERY |
                                         Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DIGITAL_AUDIO_PROCESS_V2 |
                                         Exchange::IDeviceSettingsAudio::AUDIO_CAPS_MS12),
            ::testing::Return(Core::ERROR_NONE)));

    // NOTE: this method is @deprecated (see IDeviceInfo.h); the expected literal
    // below matches IDeviceAudioCapabilities.h's actual @text tags for
    // AudioCapability (underscore-separated multi-word values, e.g.
    // "DOLBY_DIGITAL"/"Dual_Audio_Decode") and includes the "success" field the
    // interface declares — this test previously expected a stale, pre-@text
    // literal ("DOLBY DIGITAL" with a space, no "success" field) that never
    // matched the generated JSON-RPC contract.
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T(""), response));
    EXPECT_EQ(response, _T("{\"AudioCapabilities\":[\"ATMOS\",\"DOLBY_DIGITAL\",\"DOLBY_DIGITAL_PLUS\",\"Dual_Audio_Decode\",\"DAPv2\",\"MS12\"],\"success\":true}"));
}

TEST_F(DeviceInfoJsonRpcInitializedDsTest, ms12capabilities)
{
    ON_CALL(DeviceSettingsAudioMock::Mock(), GetAudioMS12Capabilities(::testing::_, ::testing::_))
        .WillByDefault(::testing::DoAll(
            ::testing::SetArgReferee<1>(Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_DOLBYVOLUME |
                                         Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_INTELLIGENT_EQUALIZER |
                                         Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_DIALOG_ENHANCER),
            ::testing::Return(Core::ERROR_NONE)));

    // See the "audiocapabilities" test above re: the corrected @text literal.
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T(""), response));
    EXPECT_EQ(response, _T("{\"MS12Capabilities\":[\"Dolby_Volume\",\"Inteligent_Equalizer\",\"Dialogue_Enhancer\"],\"success\":true}"));
}

TEST_F(DeviceInfoJsonRpcInitializedDsTest, supportedms12audioprofiles)
{
    ON_CALL(DeviceSettingsAudioMock::Mock(), GetAudioMS12ProfileList(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [](const int32_t, Exchange::IDeviceSettingsAudio::IDeviceSettingsAudioMS12AudioProfileIterator*& iter) -> Core::hresult {
                Exchange::IDeviceSettingsAudio::MS12AudioProfile entry;
                entry.audioProfile = "Movie";
                std::list<Exchange::IDeviceSettingsAudio::MS12AudioProfile> list{ entry };
                iter = (Core::Service<RPC::IteratorType<Exchange::IDeviceSettingsAudio::IDeviceSettingsAudioMS12AudioProfileIterator>>::Create<Exchange::IDeviceSettingsAudio::IDeviceSettingsAudioMS12AudioProfileIterator>(list));
                return Core::ERROR_NONE;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T(""), response));
    EXPECT_EQ(response, _T("{\"supportedMS12AudioProfiles\":[\"Movie\"],\"success\":true}"));
}
