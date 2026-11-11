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

// NOTE ON THIS FILE'S HISTORY
// ---------------------------------------------------------------------------
// This file previously tested the pre-refactor DeviceVideoCapabilities code
// path by mocking the legacy device::Host / device::VideoOutputPort HAL
// directly (getVideoOutputPorts(), getDefaultResolution(), getHDCPProtocol(),
// etc., via AudioOutputPortMock.h / HostMock.h / ManagerMock.h / VideoOutputPort*Mock.h /
// VideoResolutionMock.h). DeviceVideoCapabilities.cpp was refactored to talk to
// the real org.rdk.DeviceSettings plugin over COM-RPC via DSHelper:
//   - SupportedVideoDisplays()/DefaultResolution()/SupportedResolutions() now
//     read purely from DSHelper's cached VideoPortConfigStore, populated once
//     (lazily) from IDeviceSettings::GetDeviceSettingConfigs(). No per-call
//     COM-RPC, no device:: HAL calls, at all.
//   - SupportedHdcp() additionally uses the per-port handle cached during that
//     same config load (via IDeviceSettingsVideoPort::GetVideoPort()), then
//     calls IDeviceSettingsVideoPort::GetHDCPProtocolVersionOnVideoPort().
//   - HostEDID() calls IDeviceSettingsHost::GetEDID().
// grep for "device::" across plugin/DeviceInfo.cpp, plugin/DeviceInfoImplementation.cpp,
// plugin/DeviceAudioCapabilities.cpp and plugin/DeviceVideoCapabilities.cpp: there are
// zero real call sites left (DeviceAudioCapabilities.cpp has exactly one hit, and it's a
// comment). None of the legacy libds mocks above are needed to construct/activate
// Plugin::DeviceInfo any more, so this file does NOT include or instantiate them — see
// the "Legacy libds header/mock removal" section of the migration prompt for why keeping
// them "just in case" is the wrong default (it re-introduces the exact dependency this
// migration is meant to remove, and the entservices-testframework headers backing them
// are deleted upstream — this repo's CI currently only still compiles against them
// because entservices-testframework is pinned to a pre-deletion commit; see that prompt
// section before changing the pin).
// This file was rewritten from scratch to stub DeviceSettingsMock (the COM-RPC root) /
// DeviceSettingsVideoPortMock / DeviceSettingsHostMock instead, mirroring the pattern
// used in test_FrameRate.cpp. Several of the old exception-injection tests
// (*_Failure_DeviceException / *_Failure_StdException / *_Failure_UnknownException)
// targeted HAL call sites that no longer exist; those are kept (for test-name
// stability / CI dashboard continuity) but now exercise the single analogous
// failure surface that *does* exist in the new architecture (a COM-RPC call
// returning a non-success hresult), each with a short comment explaining the
// mapping.

#include <gtest/gtest.h>

#include "DeviceInfo.h"
#include "DeviceInfoImplementation.h"
#include "DeviceAudioCapabilities.h"
#include "DeviceVideoCapabilities.h"
#include "IarmBusMock.h"
#include "ServiceMock.h"
#include "RfcApiMock.h"
#include "COMLinkMock.h"
#include "WrapsMock.h"
#include "ISubSystemMock.h"
#include "SystemInfo.h"
#include "WorkerPoolImplementation.h"
#include "DeviceSettingsMock.h"
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "ThunderPortability.h"

using namespace WPEFramework;

using ::testing::NiceMock;
using ::testing::_;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::Invoke;

namespace {
    const string webPrefix = _T("/Service/DeviceInfo");
}

// OnDeviceSettingsActivated() runs via the WorkerPool's async job — the mock
// Register()/Activated() callback returning only confirms the notification was
// dispatched, not that DSHelper's internal config-store reset has actually run
// yet. Waiting on that instead of the notification callback closes a real race
// where a test's SetVideoPortConfig() + handler.Invoke() could still observe a
// previous test's cached config. Mirrors entservices-framerate's
// TestableFrameRateImplementation pattern.
class TestableDeviceVideoCapabilities : public Plugin::DeviceVideoCapabilities {
public:
    void OnDeviceSettingsActivated() override
    {
        Plugin::DeviceVideoCapabilities::OnDeviceSettingsActivated();
        {
            std::lock_guard<std::mutex> lock(_activationMutex);
            _activated = true;
        }
        _activationCv.notify_all();
    }

    void WaitForActivation()
    {
        std::unique_lock<std::mutex> lock(_activationMutex);
        _activationCv.wait_for(lock, std::chrono::seconds(5), [this]() { return _activated; });
    }

private:
    std::mutex _activationMutex;
    std::condition_variable _activationCv;
    bool _activated = false;
};

class DeviceVideoCapabilitiesTest : public ::testing::Test {
protected:
    Core::ProxyType<Plugin::DeviceInfo> plugin;
    Core::ProxyType<Plugin::DeviceInfoImplementation> deviceInfoImplementation;
    Core::ProxyType<Plugin::DeviceAudioCapabilities> deviceAudioCapabilities;
    Core::ProxyType<TestableDeviceVideoCapabilities> deviceVideoCapabilities;
    Core::JSONRPC::Handler& handler;
    DECL_CORE_JSONRPC_CONX connection;
    string response;

    // IARM/RFC/Wraps: still legitimately used by DeviceInfoImplementation's own
    // methods (SerialNumber/Make/Model/etc. via IARM_Bus_Call / RFC / v_secure_popen)
    // reachable through the same Plugin::DeviceInfo instance. Unrelated to
    // DeviceSettings/libds — do not remove these.
    IarmBusImplMock* p_iarmBusImplMock = nullptr;
    RfcApiImplMock* p_rfcApiImplMock = nullptr;
    WrapsImplMock* p_wrapsImplMock = nullptr;
    NiceMock<ServiceMock> service;
    NiceMock<COMLinkMock> comLinkMock;
    Core::Sink<NiceMock<SystemInfo>> subSystem;

    // See DeviceInfoTest (test_DeviceInfo.cpp) for why a single process-wide pool
    // is required instead of one per fixture.
    static WorkerPoolImplementation& SharedWorkerPool()
    {
        static Core::ProxyType<WorkerPoolImplementation> pool =
            Core::ProxyType<WorkerPoolImplementation>::Create(
                3, Core::Thread::DefaultStackSize(), 16);
        return *pool;
    }

    std::mutex deviceSettingsMutex;
    std::condition_variable deviceSettingsCondition;
    bool deviceSettingsActivated = false;

    DeviceVideoCapabilitiesTest()
        : plugin(Core::ProxyType<Plugin::DeviceInfo>::Create())
        , handler(*plugin)
        , INIT_CONX(1, 0)
    {
        p_iarmBusImplMock = new NiceMock<IarmBusImplMock>;
        IarmBus::setImpl(p_iarmBusImplMock);

        p_rfcApiImplMock = new NiceMock<RfcApiImplMock>;
        RfcApi::setImpl(p_rfcApiImplMock);

        p_wrapsImplMock = new NiceMock<WrapsImplMock>;
        Wraps::setImpl(p_wrapsImplMock);

        deviceInfoImplementation = Core::ProxyType<Plugin::DeviceInfoImplementation>::Create();
        deviceAudioCapabilities = Core::ProxyType<Plugin::DeviceAudioCapabilities>::Create();
        deviceVideoCapabilities = Core::ProxyType<TestableDeviceVideoCapabilities>::Create();

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

        if (!Core::IWorkerPool::IsAvailable()) {
            Core::IWorkerPool::Assign(&SharedWorkerPool());
            SharedWorkerPool().Run();
        }

        // DSHelper opens a COM-RPC link to DeviceSettings: the SmartInterface's
        // RegisterJob calls service.Register() then, on activation, resolves the
        // root IDeviceSettings via the shell's QueryInterface(IDeviceSettings::ID).
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

        // Pre-create the DeviceSettings root mock on the main thread so the
        // worker-thread QueryInterface() during activation only reads the
        // (already-populated) mock registry instead of racing to insert into it.
        (void)DeviceSettingsMock::Get();

        // Default config: two HDMI video output ports (HDMI0, HDMI1) each with
        // their own default resolution, and a small supported-resolution list.
        // Tests override via SetVideoPortConfig()/ON_CALL(...) before invoking
        // the JSON-RPC method under test (DSHelper loads config lazily, once,
        // on the first accessor call per test).
        // supportedResolutionNames is a per-TYPE property in the real config schema
        // (SetVideoPortConfig's map keys videoPortTypes by type, not by port index) —
        // ports sharing a type must use the same resolution list, only
        // defaultResolution legitimately varies per port.
        SetVideoPortConfig({
            { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 0, "1080p", "480p,720p,1080p,2160p" },
            { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 1, "720p",  "480p,720p,1080p,2160p" },
        });

        // SupportedHdcp() additionally needs a per-port COM-RPC handle (acquired
        // during LoadAllConfigs()'s Phase 3) and the HDCP query itself. Default to
        // a deterministic handle=type*100+index mapping and HDCP 2.2, mirroring
        // test_DeviceAudioCapabilities.cpp's audioPortName()/handle convention.
        ON_CALL(DeviceSettingsVideoPortMock::Mock(), GetVideoPort(::testing::_, ::testing::_, ::testing::_))
            .WillByDefault(::testing::Invoke(
                [](const Exchange::IDeviceSettingsVideoPort::VideoPort type, const int32_t index, int32_t& handle) -> Core::hresult {
                    handle = static_cast<int32_t>(type) * 100 + index;
                    return Core::ERROR_NONE;
                }));
        ON_CALL(DeviceSettingsVideoPortMock::Mock(), GetHDCPProtocolVersionOnVideoPort(::testing::_, ::testing::_))
            .WillByDefault(::testing::DoAll(
                ::testing::SetArgReferee<1>(Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_2X),
                ::testing::Return(Core::ERROR_NONE)));

        EXPECT_EQ(string(""), plugin->Initialize(&service));

        {
            std::unique_lock<std::mutex> lock(deviceSettingsMutex);
            deviceSettingsCondition.wait_for(
                lock, std::chrono::seconds(5), [this]() { return deviceSettingsActivated; });
        }

        // See TestableDeviceVideoCapabilities: wait for DSHelper's own async
        // activation handler to actually run, not just for the mock notification
        // to have been dispatched.
        deviceVideoCapabilities->WaitForActivation();
    }

    virtual ~DeviceVideoCapabilitiesTest()
    {
        plugin->Deinitialize(&service);

        // Cascades to DeviceSettingsVideoPortMock::Delete()/DeviceSettingsHostMock::Delete()
        // (see DeviceSettingsMock::Delete()).
        DeviceSettingsMock::Delete();

        Wraps::setImpl(nullptr);
        if (p_wrapsImplMock != nullptr) {
            delete p_wrapsImplMock;
            p_wrapsImplMock = nullptr;
        }

        RfcApi::setImpl(nullptr);
        if (p_rfcApiImplMock != nullptr) {
            delete p_rfcApiImplMock;
            p_rfcApiImplMock = nullptr;
        }

        IarmBus::setImpl(nullptr);
        if (p_iarmBusImplMock != nullptr) {
            delete p_iarmBusImplMock;
            p_iarmBusImplMock = nullptr;
        }
    }

    // ---- Test helpers -----------------------------------------------------

    struct VideoPortSpec {
        Exchange::IDeviceSettingsVideoPort::VideoPort type;
        int32_t index;
        std::string defaultResolution;
        std::string supportedResolutionNames; // comma-separated; applies to the whole type
    };

    // (Re)configures the mocked DeviceSettings root's GetDeviceSettingConfigs()
    // response from a list of video ports. Safe to call again from inside a
    // TEST_F body, as long as it happens before the first JSON-RPC call in that
    // test (config is loaded lazily, once, and then cached for the rest of the
    // test).
    void SetVideoPortConfig(const std::vector<VideoPortSpec>& ports)
    {
        ON_CALL(DeviceSettingsMock::Mock(), GetDeviceSettingConfigs(::testing::_))
            .WillByDefault(::testing::Invoke(
                [ports](Exchange::IDeviceSettings::DeviceSettingConfigs& configs) -> Core::hresult {
                    std::map<int32_t, std::string> resolutionsByType;
                    for (const auto& p : ports) {
                        resolutionsByType[static_cast<int32_t>(p.type)] = p.supportedResolutionNames;
                    }
                    for (const auto& kv : resolutionsByType) {
                        Exchange::IDeviceSettings::VideoPortTypeConfig typeCfg{};
                        typeCfg.typeId = kv.first;
                        typeCfg.dtcpSupported = false;
                        typeCfg.hdcpSupported = true;
                        typeCfg.restrictedResolution = 0;
                        typeCfg.supportedResolutionNames = kv.second;
                        configs.videoPortTypes.push_back(typeCfg);
                    }
                    for (const auto& p : ports) {
                        Exchange::IDeviceSettings::VideoPortPortConfig portCfg{};
                        portCfg.videoPortType = static_cast<int32_t>(p.type);
                        portCfg.videoPortIndex = p.index;
                        portCfg.connectedAudioPortType = 0;
                        portCfg.connectedAudioPortIndex = 0;
                        portCfg.defaultResolution = p.defaultResolution;
                        configs.videoPorts.push_back(portCfg);
                    }
                    return Core::ERROR_NONE;
                }));
    }

    // Config succeeds but reports zero video ports at all (distinct from "port
    // name not found" - this is "no config loaded" / "DeviceSettings has no
    // video output ports configured").
    void SetEmptyVideoPortConfig()
    {
        SetVideoPortConfig({});
    }
};

// =========== SupportedVideoDisplays ===========

TEST_F(DeviceVideoCapabilitiesTest, SupportedVideoDisplays_Success_SingleDisplay)
{
    SetVideoPortConfig({
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 0, "1080p", "1080p" },
    });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedvideodisplays"), _T(""), response));
    EXPECT_TRUE(response.find("\"supportedVideoDisplays\":[") != string::npos);
    EXPECT_TRUE(response.find("\"HDMI0\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedVideoDisplays_Success_MultipleDisplays)
{
    // Default fixture config already has HDMI0 + HDMI1.
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedvideodisplays"), _T(""), response));
    EXPECT_TRUE(response.find("\"HDMI0\"") != string::npos);
    EXPECT_TRUE(response.find("\"HDMI1\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedVideoDisplays_Success_DuplicatePortsFiltered)
{
    // The raw GetDeviceSettingConfigs() response can legitimately contain the same
    // (type, index) video port twice (e.g. a config bug, or two callers merging
    // configs) — VideoPortConfigStore does not deduplicate its portConfigs vector.
    // DeviceVideoCapabilities::SupportedVideoDisplays() must still de-duplicate the
    // resulting *names* before returning them.
    SetVideoPortConfig({
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 0, "1080p", "1080p" },
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 0, "1080p", "1080p" },
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 1, "720p",  "720p" },
    });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedvideodisplays"), _T(""), response));
    EXPECT_TRUE(response.find("\"HDMI0\"") != string::npos);
    EXPECT_TRUE(response.find("\"HDMI1\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);

    // "HDMI0" should appear exactly once even though two identical (type, index)
    // entries were supplied.
    const size_t first = response.find("\"HDMI0\"");
    EXPECT_EQ(response.find("\"HDMI0\"", first + 1), string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedVideoDisplays_Success_EmptyList)
{
    // Config loads successfully but reports zero video ports -> the accessor
    // itself reports "no entries" -> ERROR_UNAVAILABLE, not an empty success list
    // (see AudioConfigStore::getAudioPortEntries()'s analogous "!entries.empty()"
    // contract in test_DeviceInfo.cpp's SupportedAudioPorts_Negative_EmptyPortList).
    SetEmptyVideoPortConfig();

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, handler.Invoke(connection, _T("supportedvideodisplays"), _T(""), response));
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedVideoDisplays_Failure_DeviceException)
{
    // No HAL exception surface exists any more; the analogous failure at the new
    // COM-RPC boundary is GetDeviceSettingConfigs() itself failing.
    ON_CALL(DeviceSettingsMock::Mock(), GetDeviceSettingConfigs(::testing::_))
        .WillByDefault(::testing::Return(Core::ERROR_GENERAL));

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, handler.Invoke(connection, _T("supportedvideodisplays"), _T(""), response));
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedVideoDisplays_Failure_StdException)
{
    // Same underlying failure surface as *_Failure_DeviceException above.
    SetEmptyVideoPortConfig();

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, handler.Invoke(connection, _T("supportedvideodisplays"), _T(""), response));
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedVideoDisplays_Failure_UnknownException)
{
    // Same underlying failure surface: DeviceSettings COM-RPC unavailable/erroring.
    ON_CALL(DeviceSettingsMock::Mock(), GetDeviceSettingConfigs(::testing::_))
        .WillByDefault(::testing::Return(Core::ERROR_GENERAL));

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, handler.Invoke(connection, _T("supportedvideodisplays"), _T(""), response));
}

// =========== HostEDID ===========

TEST_F(DeviceVideoCapabilitiesTest, HostEDID_Success_ValidEDID)
{
    ON_CALL(DeviceSettingsHostMock::Mock(), GetEDID(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [](uint8_t edId[], const uint16_t edIdLength) -> Core::hresult {
                edId[0] = 0x00; edId[1] = 0xFF; edId[2] = 0xFF; edId[3] = 0xFF;
                return Core::ERROR_NONE;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("hostedid"), _T(""), response));
    EXPECT_TRUE(response.find("\"EDID\":") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, HostEDID_Success_EmptyEDID)
{
    // GetEDID() succeeds but every byte of the (fixed 256-byte) buffer is zero;
    // HostEDID() falls back to encoding the full buffer rather than a zero-length
    // string (see the "if (actualLen == 0) actualLen = kEdidBufLen;" safety net).
    ON_CALL(DeviceSettingsHostMock::Mock(), GetEDID(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Core::ERROR_NONE));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("hostedid"), _T(""), response));
    EXPECT_TRUE(response.find("\"EDID\":") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, HostEDID_Failure_DeviceException)
{
    // No HAL exception surface exists any more; the analogous failure at the new
    // COM-RPC boundary is GetEDID() itself failing.
    ON_CALL(DeviceSettingsHostMock::Mock(), GetEDID(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Core::ERROR_GENERAL));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("hostedid"), _T(""), response));
}

TEST_F(DeviceVideoCapabilitiesTest, HostEDID_Failure_StdException)
{
    // Same underlying failure surface as *_Failure_DeviceException above.
    ON_CALL(DeviceSettingsHostMock::Mock(), GetEDID(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Core::ERROR_GENERAL));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("hostedid"), _T(""), response));
}

TEST_F(DeviceVideoCapabilitiesTest, HostEDID_Failure_UnknownException)
{
    // Same underlying failure surface: DeviceSettings COM-RPC unavailable/erroring.
    ON_CALL(DeviceSettingsHostMock::Mock(), GetEDID(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Core::ERROR_GENERAL));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("hostedid"), _T(""), response));
}

// =========== DefaultResolution ===========

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Success_EmptyPort)
{
    // Empty videoDisplay -> DSHelper::getDefaultVideoPortName() picks HDMI0 (the
    // fixture's default config), whose defaultResolution is "1080p".
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"defaultResolution\":\"1080p\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Success_SpecificPort)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI1\"}"), response));
    EXPECT_TRUE(response.find("\"defaultResolution\":\"720p\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Success_4KResolution)
{
    SetVideoPortConfig({
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 0, "2160p", "2160p" },
    });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"defaultResolution\":\"2160p\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Failure_DeviceException)
{
    // No HAL exception surface exists any more; DefaultResolution() is a pure
    // config-store lookup, so the only failure mode is "requested port isn't in
    // the loaded config" -> Core::ERROR_NOT_EXIST (not ERROR_GENERAL).
    EXPECT_EQ(Core::ERROR_NOT_EXIST, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"DOES_NOT_EXIST\"}"), response));
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Failure_StdException)
{
    // Same underlying failure surface as *_Failure_DeviceException above.
    EXPECT_EQ(Core::ERROR_NOT_EXIST, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"NOT_A_PORT\"}"), response));
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Failure_UnknownException)
{
    // Same underlying failure surface as above.
    EXPECT_EQ(Core::ERROR_NOT_EXIST, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"UNKNOWN\"}"), response));
}

// =========== SupportedResolutions ===========

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Success_EmptyPort_MultipleResolutions)
{
    // Empty videoDisplay -> default port HDMI0, whose type has 4 supported resolutions.
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"supportedResolutions\":[") != string::npos);
    EXPECT_TRUE(response.find("\"480p\"") != string::npos);
    EXPECT_TRUE(response.find("\"720p\"") != string::npos);
    EXPECT_TRUE(response.find("\"1080p\"") != string::npos);
    EXPECT_TRUE(response.find("\"2160p\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Success_SpecificPort_SingleResolution)
{
    SetVideoPortConfig({
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 1, "1080p", "1080p" },
    });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"HDMI1\"}"), response));
    EXPECT_TRUE(response.find("\"1080p\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Success_EmptyList)
{
    // Port exists (resolves), but its type config has no supported resolutions
    // recorded at all -> the CSV parse loop yields an empty list; still success.
    SetVideoPortConfig({
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 0, "1080p", "" },
    });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Failure_DeviceException)
{
    // Pure config-store lookup; the only failure mode is "requested port isn't in
    // the loaded config" -> Core::ERROR_NOT_EXIST.
    EXPECT_EQ(Core::ERROR_NOT_EXIST, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"DOES_NOT_EXIST\"}"), response));
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Failure_StdException)
{
    EXPECT_EQ(Core::ERROR_NOT_EXIST, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"NOT_A_PORT\"}"), response));
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Failure_UnknownException)
{
    EXPECT_EQ(Core::ERROR_NOT_EXIST, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"UNKNOWN\"}"), response));
}

// =========== SupportedHdcp ===========

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Success_EmptyPort_HDCP22)
{
    ON_CALL(DeviceSettingsVideoPortMock::Mock(), GetHDCPProtocolVersionOnVideoPort(::testing::_, ::testing::_))
        .WillByDefault(::testing::DoAll(
            ::testing::SetArgReferee<1>(Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_2X),
            ::testing::Return(Core::ERROR_NONE)));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"supportedHDCPVersion\":\"2.2\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Success_SpecificPort_HDCP14)
{
    ON_CALL(DeviceSettingsVideoPortMock::Mock(), GetHDCPProtocolVersionOnVideoPort(::testing::_, ::testing::_))
        .WillByDefault(::testing::DoAll(
            ::testing::SetArgReferee<1>(Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_1X),
            ::testing::Return(Core::ERROR_NONE)));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI1\"}"), response));
    EXPECT_TRUE(response.find("\"supportedHDCPVersion\":\"1.4\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Success_EmptyPort_HDCP14)
{
    ON_CALL(DeviceSettingsVideoPortMock::Mock(), GetHDCPProtocolVersionOnVideoPort(::testing::_, ::testing::_))
        .WillByDefault(::testing::DoAll(
            ::testing::SetArgReferee<1>(Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_1X),
            ::testing::Return(Core::ERROR_NONE)));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"supportedHDCPVersion\":\"1.4\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Success_SpecificPort_HDCP22)
{
    ON_CALL(DeviceSettingsVideoPortMock::Mock(), GetHDCPProtocolVersionOnVideoPort(::testing::_, ::testing::_))
        .WillByDefault(::testing::DoAll(
            ::testing::SetArgReferee<1>(Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_2X),
            ::testing::Return(Core::ERROR_NONE)));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"supportedHDCPVersion\":\"2.2\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Failure_InvalidProtocol)
{
    // GetHDCPProtocolVersionOnVideoPort() succeeds but reports a version the
    // switch in SupportedHdcp() doesn't recognize -> Core::ERROR_GENERAL.
    ON_CALL(DeviceSettingsVideoPortMock::Mock(), GetHDCPProtocolVersionOnVideoPort(::testing::_, ::testing::_))
        .WillByDefault(::testing::DoAll(
            ::testing::SetArgReferee<1>(Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_MAX),
            ::testing::Return(Core::ERROR_NONE)));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Failure_DeviceException)
{
    // No HAL exception surface exists any more; the analogous failure at the new
    // COM-RPC boundary is GetHDCPProtocolVersionOnVideoPort() itself failing.
    ON_CALL(DeviceSettingsVideoPortMock::Mock(), GetHDCPProtocolVersionOnVideoPort(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Core::ERROR_GENERAL));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"\"}"), response));
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Failure_StdException)
{
    // Same underlying failure surface as *_Failure_DeviceException above.
    ON_CALL(DeviceSettingsVideoPortMock::Mock(), GetHDCPProtocolVersionOnVideoPort(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Core::ERROR_GENERAL));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"\"}"), response));
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Failure_UnknownException)
{
    // Same underlying failure surface as above.
    ON_CALL(DeviceSettingsVideoPortMock::Mock(), GetHDCPProtocolVersionOnVideoPort(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Core::ERROR_GENERAL));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
}

// =========== Additional Negative Tests ===========

TEST_F(DeviceVideoCapabilitiesTest, SupportedVideoDisplays_Negative_GetNameThrowsException)
{
    // DSHelper's getVideoPortName() is a pure switch-on-enum helper that cannot
    // throw; the equivalent robustness scenario in the new architecture is a
    // video port type the switch doesn't recognize. It must still resolve (via
    // the "default: VIDEO<index>" case) instead of failing the whole call.
    SetVideoPortConfig({
        { static_cast<Exchange::IDeviceSettingsVideoPort::VideoPort>(0xEF), 0, "1080p", "1080p" },
    });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedvideodisplays"), _T(""), response));
    EXPECT_TRUE(response.find("\"supportedVideoDisplays\":[") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, HostEDID_Negative_EDIDSizeExceedsLimit)
{
    // GetEDID() writes into a fixed 256-byte buffer over COM-RPC, so an
    // out-of-bounds write is no longer possible; the closest boundary case is a
    // buffer with NO trailing zero padding at all (every byte non-zero), which
    // must still be handled (encoded at its full length) without failing.
    ON_CALL(DeviceSettingsHostMock::Mock(), GetEDID(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [](uint8_t edId[], const uint16_t edIdLength) -> Core::hresult {
                for (uint16_t i = 0; i < edIdLength; ++i) { edId[i] = 0xAB; }
                return Core::ERROR_NONE;
            }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("hostedid"), _T(""), response));
    EXPECT_TRUE(response.find("\"EDID\":") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, HostEDID_Negative_GetHostEDIDThrowsInvoke)
{
    // Same underlying failure surface as HostEDID_Failure_*: GetEDID() reporting
    // a COM-RPC failure.
    ON_CALL(DeviceSettingsHostMock::Mock(), GetEDID(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Core::ERROR_GENERAL));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("hostedid"), _T(""), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Negative_InvalidVideoDisplay)
{
    EXPECT_EQ(Core::ERROR_NOT_EXIST, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"INVALID_PORT\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Negative_GetDefaultResolutionThrows)
{
    // Port resolves (it's in the config) but has no recorded default resolution
    // -> DSHelper::getVideoPortDefaultResolution() returns "" -> ERROR_NOT_EXIST.
    SetVideoPortConfig({
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 0, "", "1080p" },
    });

    EXPECT_EQ(Core::ERROR_NOT_EXIST, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Negative_GetNameThrowsException)
{
    EXPECT_EQ(Core::ERROR_NOT_EXIST, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"NOT_A_REAL_PORT\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Negative_GetDefaultVideoPortNameThrows)
{
    // No video ports configured at all: DSHelper::getDefaultVideoPortName() falls
    // back to the hardcoded "HDMI0", which then also fails to resolve.
    SetEmptyVideoPortConfig();

    EXPECT_EQ(Core::ERROR_NOT_EXIST, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Negative_InvalidVideoDisplay)
{
    EXPECT_EQ(Core::ERROR_NOT_EXIST, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"INVALID_PORT\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Negative_GetTypeThrowsException)
{
    EXPECT_EQ(Core::ERROR_NOT_EXIST, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"HDMI9\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Negative_GetPortTypeThrowsException)
{
    EXPECT_EQ(Core::ERROR_NOT_EXIST, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"BOGUS\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Negative_GetSupportedResolutionsThrows)
{
    // Port resolves but its type config has an empty supported-resolutions list
    // -> success with an empty array, not a failure (see *_Success_EmptyList).
    SetVideoPortConfig({
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 0, "1080p", "" },
    });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Negative_ResolutionGetNameThrows)
{
    EXPECT_EQ(Core::ERROR_NOT_EXIST, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"UNRESOLVABLE\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Negative_GetDefaultVideoPortNameThrows)
{
    SetEmptyVideoPortConfig();

    EXPECT_EQ(Core::ERROR_NOT_EXIST, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Negative_InvalidVideoDisplay)
{
    EXPECT_EQ(Core::ERROR_NOT_EXIST, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"INVALID_PORT\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Negative_GetHDCPProtocolThrows)
{
    ON_CALL(DeviceSettingsVideoPortMock::Mock(), GetHDCPProtocolVersionOnVideoPort(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Core::ERROR_GENERAL));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Negative_GetDefaultVideoPortNameThrows)
{
    SetEmptyVideoPortConfig();

    EXPECT_EQ(Core::ERROR_NOT_EXIST, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Negative_GetPortFromConfigThrows)
{
    // Port resolves in config, but its COM-RPC handle was never cached (GetVideoPort()
    // failed for it during LoadAllConfigs()'s Phase 3) -> Core::ERROR_UNAVAILABLE,
    // distinct from "port doesn't resolve at all" (ERROR_NOT_EXIST).
    ON_CALL(DeviceSettingsVideoPortMock::Mock(), GetVideoPort(::testing::_, ::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Core::ERROR_GENERAL));
    SetVideoPortConfig({
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 0, "1080p", "1080p" },
    });

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

// =========== Additional Comprehensive Positive Tests ===========

TEST_F(DeviceVideoCapabilitiesTest, SupportedVideoDisplays_Positive_SingleDisplay)
{
    SetVideoPortConfig({
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 0, "1080p", "1080p" },
    });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedvideodisplays"), _T("{}"), response));
    EXPECT_TRUE(response.find("\"HDMI0\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedVideoDisplays_Positive_VariousPortTypes)
{
    SetVideoPortConfig({
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI,      0, "1080p", "1080p" },
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI,      1, "1080p", "1080p" },
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_COMPONENT, 0, "480i",  "480i" },
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_DVI,       0, "1080p", "1080p" },
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_SVIDEO,    0, "480i",  "480i" },
    });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedvideodisplays"), _T("{}"), response));
    EXPECT_TRUE(response.find("\"HDMI0\"") != string::npos);
    EXPECT_TRUE(response.find("\"HDMI1\"") != string::npos);
    EXPECT_TRUE(response.find("\"Component0\"") != string::npos);
    EXPECT_TRUE(response.find("\"DVI0\"") != string::npos);
    EXPECT_TRUE(response.find("\"SVideo0\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Positive_VariousResolutions)
{
    // Config loads once per test (lazily, on the first accessor call) and is then
    // cached, so re-calling SetVideoPortConfig() mid-test does NOT trigger a
    // reload — verify multiple distinct resolutions via separate port indices
    // configured up front in a single call, not by looping SetVideoPortConfig().
    static const char* const resolutions[] = { "1080p", "720p", "2160p", "480i" };
    SetVideoPortConfig({
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 0, resolutions[0], resolutions[0] },
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 1, resolutions[1], resolutions[1] },
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 2, resolutions[2], resolutions[2] },
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 3, resolutions[3], resolutions[3] },
    });

    for (int32_t index = 0; index < 4; ++index) {
        const string videoDisplay = string("HDMI") + std::to_string(index);
        EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"") + videoDisplay + _T("\"}"), response));
        EXPECT_TRUE(response.find(string("\"") + resolutions[index] + "\"") != string::npos);
    }
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Positive_VariousPortNames)
{
    SetVideoPortConfig({
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI,      0, "1080p", "1080p" },
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI,      1, "720p",  "720p" },
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_COMPONENT, 0, "480i",  "480i" },
    });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"1080p\"") != string::npos);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI1\"}"), response));
    EXPECT_TRUE(response.find("\"720p\"") != string::npos);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"Component0\"}"), response));
    EXPECT_TRUE(response.find("\"480i\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Positive_DefaultPortUsed)
{
    SetVideoPortConfig({
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 1, "1080p", "1080p" },
    });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"1080p\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Positive_SingleResolution)
{
    SetVideoPortConfig({
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 0, "1080p", "1080p" },
    });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"1080p\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Positive_LargeResolutionList)
{
    SetVideoPortConfig({
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 0, "1080p", "2160p,1080p,1080i,720p,576p,480p,480i" },
    });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"2160p\"") != string::npos);
    EXPECT_TRUE(response.find("\"1080p\"") != string::npos);
    EXPECT_TRUE(response.find("\"1080i\"") != string::npos);
    EXPECT_TRUE(response.find("\"720p\"") != string::npos);
    EXPECT_TRUE(response.find("\"576p\"") != string::npos);
    EXPECT_TRUE(response.find("\"480p\"") != string::npos);
    EXPECT_TRUE(response.find("\"480i\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Positive_VariousPortNames)
{
    SetVideoPortConfig({
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI,      0, "1080p", "1080p,720p" },
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_COMPONENT, 0, "480i",  "480i" },
    });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"1080p\"") != string::npos);
    EXPECT_TRUE(response.find("\"720p\"") != string::npos);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"Component0\"}"), response));
    EXPECT_TRUE(response.find("\"480i\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Positive_DefaultPortUsed)
{
    SetVideoPortConfig({
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 1, "1080p", "1080p" },
    });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"1080p\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Positive_VariousHdcpVersions)
{
    ON_CALL(DeviceSettingsVideoPortMock::Mock(), GetHDCPProtocolVersionOnVideoPort(::testing::_, ::testing::_))
        .WillByDefault(::testing::DoAll(
            ::testing::SetArgReferee<1>(Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_1X),
            ::testing::Return(Core::ERROR_NONE)));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"supportedHDCPVersion\":\"1.4\"") != string::npos);

    ON_CALL(DeviceSettingsVideoPortMock::Mock(), GetHDCPProtocolVersionOnVideoPort(::testing::_, ::testing::_))
        .WillByDefault(::testing::DoAll(
            ::testing::SetArgReferee<1>(Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_2X),
            ::testing::Return(Core::ERROR_NONE)));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"supportedHDCPVersion\":\"2.2\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Positive_VariousPortNames)
{
    SetVideoPortConfig({
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 0, "1080p", "1080p" },
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 1, "1080p", "1080p" },
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 2, "1080p", "1080p" },
    });

    ON_CALL(DeviceSettingsVideoPortMock::Mock(), GetHDCPProtocolVersionOnVideoPort(::testing::_, ::testing::_))
        .WillByDefault(::testing::DoAll(
            ::testing::SetArgReferee<1>(Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_2X),
            ::testing::Return(Core::ERROR_NONE)));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"supportedHDCPVersion\":\"2.2\"") != string::npos);

    ON_CALL(DeviceSettingsVideoPortMock::Mock(), GetHDCPProtocolVersionOnVideoPort(::testing::_, ::testing::_))
        .WillByDefault(::testing::DoAll(
            ::testing::SetArgReferee<1>(Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_1X),
            ::testing::Return(Core::ERROR_NONE)));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI1\"}"), response));
    EXPECT_TRUE(response.find("\"supportedHDCPVersion\":\"1.4\"") != string::npos);

    ON_CALL(DeviceSettingsVideoPortMock::Mock(), GetHDCPProtocolVersionOnVideoPort(::testing::_, ::testing::_))
        .WillByDefault(::testing::DoAll(
            ::testing::SetArgReferee<1>(Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_2X),
            ::testing::Return(Core::ERROR_NONE)));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI2\"}"), response));
    EXPECT_TRUE(response.find("\"supportedHDCPVersion\":\"2.2\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Positive_DefaultPortUsed)
{
    SetVideoPortConfig({
        { Exchange::IDeviceSettingsVideoPort::DS_VIDEO_PORT_TYPE_HDMI, 0, "1080p", "1080p" },
    });
    ON_CALL(DeviceSettingsVideoPortMock::Mock(), GetHDCPProtocolVersionOnVideoPort(::testing::_, ::testing::_))
        .WillByDefault(::testing::DoAll(
            ::testing::SetArgReferee<1>(Exchange::IDeviceSettingsVideoPort::DS_HDCP_VERSION_2X),
            ::testing::Return(Core::ERROR_NONE)));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"supportedHDCPVersion\":\"2.2\"") != string::npos);
}

// =========== Boundary and Edge Case Tests ===========

TEST_F(DeviceVideoCapabilitiesTest, EdgeCase_SequentialCallsSamePort)
{
    // First call - defaultresolution
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"1080p\"") != string::npos);

    // Second call, same port - supportedhdcp
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"supportedHDCPVersion\":\"2.2\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, EdgeCase_AlternatingPortCalls)
{
    // Config is loaded once and cached: alternating between two ports across
    // multiple calls must consistently return each port's own data.
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"1080p\"") != string::npos);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI1\"}"), response));
    EXPECT_TRUE(response.find("\"720p\"") != string::npos);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"1080p\"") != string::npos);
}
