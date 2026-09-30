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
// This file previously bridged the new COM-RPC mocks (DeviceSettingsMock /
// DeviceSettingsAudioMock) back into the legacy device::Host / AudioOutputPortMock
// HAL mocks purely so the pre-refactor test bodies (EXPECT_CALL on
// p_audioOutputPortMock/p_hostImplMock) could be reused unchanged. That bridge lived
// entirely in this test file, not in plugin/DeviceAudioCapabilities.cpp (grep it:
// there is exactly one hit for "device::", and it's a comment) — it added an
// indirection this migration is meant to remove, and depended on legacy libds mock
// headers that are deleted upstream (this repo's CI currently only still compiles
// against them because entservices-testframework is pinned to a pre-deletion
// commit; see the migration prompt's "Dependency repo pinning" section).
//
// DeviceAudioCapabilities::AudioCapabilities()/MS12Capabilities()/
// SupportedMS12AudioProfiles() resolve the requested audioPort against DSHelper's
// cached AudioConfigStore (populated once, lazily, from
// IDeviceSettings::GetDeviceSettingConfigs()), then call
// IDeviceSettingsAudio::GetAudioCapabilities()/GetAudioMS12Capabilities()/
// GetAudioMS12ProfileList() directly over COM-RPC using the port's cached handle
// (acquired via IDeviceSettingsAudio::GetAudioPort() during that same config load).
// This file now stubs DeviceSettingsMock/DeviceSettingsAudioMock directly, mirroring
// test_FrameRate.cpp / the rewritten test_DeviceVideoCapabilities.cpp.
//
// Two behavioral notes that differ from the old device::Host-based semantics:
//   - AudioCapabilities()/MS12Capabilities() resolve an *empty* audioPort to
//     whichever config entry is *first* in DSHelper's cached list (not a queried
//     "default port name") — see the `audioPort.empty() || entries[i].name == audioPort`
//     check. SupportedMS12AudioProfiles() is the odd one out: it explicitly calls
//     DSHelper::getDefaultAudioPortName() (prefers HDMI0, else SPEAKER0, else the
//     first entry) for an empty audioPort.
//   - A port name that isn't found in the loaded config (or whose handle wasn't
//     cached) is NOT an error: AudioCapabilities()/MS12Capabilities() report
//     AUDIOCAPABILITY_NONE/MS12CAPABILITY_NONE with success=true, and
//     SupportedMS12AudioProfiles() reports an empty list with success=true.
// The old HAL-exception-injection tests (*_Failure_*Exception, *_Negative_Get*Throws)
// targeted call sites that no longer exist; those are kept (for test-name stability /
// CI dashboard continuity) but now exercise the actual failure surfaces that do exist
// in the new architecture: either the COM-RPC capability/profile-list call itself
// returning a non-success hresult (-> Core::ERROR_GENERAL, propagated as-is), or no
// audio ports being configured at all (-> Core::ERROR_UNAVAILABLE, same contract as
// test_DeviceInfo.cpp's SupportedAudioPorts_Negative_EmptyPortList).

#include <gtest/gtest.h>

#include "DeviceInfo.h"
#include "DeviceAudioCapabilities.h"
#include "IarmBusMock.h"
#include "ServiceMock.h"
#include "COMLinkMock.h"
#include "DeviceSettingsMock.h"
#include <condition_variable>
#include <fstream>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "ThunderPortability.h"

using namespace WPEFramework;

using ::testing::NiceMock;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;

namespace {
const string webPrefix = _T("/Service/DeviceInfo");
}

class DeviceAudioCapabilitiesTest : public ::testing::Test {
protected:
    Core::ProxyType<Plugin::DeviceInfo> plugin;
    Core::JSONRPC::Handler& handler;
    DECL_CORE_JSONRPC_CONX connection;
    string response;

    // IARM: still legitimately used by DeviceInfoImplementation's own methods
    // reachable through the same Plugin::DeviceInfo instance. Unrelated to
    // DeviceSettings/libds — do not remove.
    IarmBusImplMock* p_iarmBusImplMock = nullptr;
    NiceMock<ServiceMock> service;
    NiceMock<COMLinkMock> comLinkMock;
    std::mutex deviceSettingsMutex;
    std::condition_variable deviceSettingsCondition;
    bool deviceSettingsActivated = false;

    DeviceAudioCapabilitiesTest()
        : plugin(Core::ProxyType<Plugin::DeviceInfo>::Create())
        , handler(*plugin)
        , INIT_CONX(1, 0)
    {
        p_iarmBusImplMock = new NiceMock<IarmBusImplMock>;
        IarmBus::setImpl(p_iarmBusImplMock);

        ON_CALL(service, ConfigLine())
            .WillByDefault(Return("{\"root\":{\"mode\":\"Off\"}}"));
        ON_CALL(service, WebPrefix())
            .WillByDefault(Return(webPrefix));
        ON_CALL(service, COMLink())
            .WillByDefault(Return(&comLinkMock));

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

        // Default config: HDMI0, HDMI1, SPDIF0, SPEAKER0 — in this order, so
        // entries[0] == "HDMI0" for AudioCapabilities()/MS12Capabilities()'s
        // "empty audioPort resolves to the first config entry" behavior, while
        // still covering every named port most tests below ask for directly.
        SetAudioPortConfig({
            { Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_HDMI,    0 },
            { Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_HDMI,    1 },
            { Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_SPDIF,   0 },
            { Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_SPEAKER, 0 },
        });

        // Deterministic handle=type*100+index mapping, acquired during
        // LoadAllConfigs()'s Phase 3 (IDeviceSettingsAudio::GetAudioPort()).
        ON_CALL(DeviceSettingsAudioMock::Mock(), GetAudioPort(::testing::_, ::testing::_, ::testing::_))
            .WillByDefault(::testing::Invoke(
                [](const Exchange::IDeviceSettingsAudio::AudioPortType type, const int32_t index, int32_t& handle) -> Core::hresult {
                    handle = static_cast<int32_t>(type) * 100 + index;
                    return Core::ERROR_NONE;
                }));

        EXPECT_EQ(string(""), plugin->Initialize(&service));

        {
            std::unique_lock<std::mutex> lock(deviceSettingsMutex);
            deviceSettingsCondition.wait_for(
                lock, std::chrono::seconds(5), [this]() { return deviceSettingsActivated; });
        }

        // Bound-delay for the async OnDeviceSettingsActivated() job (see prompt.md).
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    virtual ~DeviceAudioCapabilitiesTest()
    {
        plugin->Deinitialize(&service);

        // Cascades to DeviceSettingsAudioMock::Delete() (see DeviceSettingsMock::Delete()).
        DeviceSettingsMock::Delete();

        IarmBus::setImpl(nullptr);
        if (p_iarmBusImplMock != nullptr) {
            delete p_iarmBusImplMock;
            p_iarmBusImplMock = nullptr;
        }
    }

    // ---- Test helpers -------------------------------------------------

    struct AudioPortSpec {
        Exchange::IDeviceSettingsAudio::AudioPortType type;
        int32_t index;
    };

    // (Re)configures the mocked DeviceSettings root's GetDeviceSettingConfigs()
    // response from a list of audio ports. Safe to call again from inside a
    // TEST_F body, as long as it happens before the first JSON-RPC call in that
    // test (config is loaded lazily, once, and then cached for the rest of the
    // test).
    void SetAudioPortConfig(const std::vector<AudioPortSpec>& ports)
    {
        ON_CALL(DeviceSettingsMock::Mock(), GetDeviceSettingConfigs(::testing::_))
            .WillByDefault(::testing::Invoke(
                [ports](Exchange::IDeviceSettings::DeviceSettingConfigs& configs) -> Core::hresult {
                    for (const auto& p : ports) {
                        Exchange::IDeviceSettings::AudioPortConfigInfo portCfg{};
                        portCfg.audioPortType = static_cast<int32_t>(p.type);
                        portCfg.audioPortIndex = p.index;
                        portCfg.connectedVideoPortType = 0;
                        portCfg.connectedVideoPortIndex = 0;
                        configs.audioPorts.push_back(portCfg);
                    }
                    return Core::ERROR_NONE;
                }));
    }

    // No audio ports configured at all: distinct from "named port not found"
    // (AUDIOCAPABILITY_NONE + success) — this is DSHelper::getAudioPortEntries()
    // itself failing, which surfaces as Core::ERROR_UNAVAILABLE.
    void SetEmptyAudioPortConfig() { SetAudioPortConfig({}); }

    void SetAudioCapabilities(int32_t caps)
    {
        ON_CALL(DeviceSettingsAudioMock::Mock(), GetAudioCapabilities(::testing::_, ::testing::_))
            .WillByDefault(::testing::DoAll(
                ::testing::SetArgReferee<1>(caps),
                ::testing::Return(Core::ERROR_NONE)));
    }

    void SetAudioCapabilitiesError()
    {
        ON_CALL(DeviceSettingsAudioMock::Mock(), GetAudioCapabilities(::testing::_, ::testing::_))
            .WillByDefault(::testing::Return(Core::ERROR_GENERAL));
    }

    void SetMS12Capabilities(int32_t caps)
    {
        ON_CALL(DeviceSettingsAudioMock::Mock(), GetAudioMS12Capabilities(::testing::_, ::testing::_))
            .WillByDefault(::testing::DoAll(
                ::testing::SetArgReferee<1>(caps),
                ::testing::Return(Core::ERROR_NONE)));
    }

    void SetMS12CapabilitiesError()
    {
        ON_CALL(DeviceSettingsAudioMock::Mock(), GetAudioMS12Capabilities(::testing::_, ::testing::_))
            .WillByDefault(::testing::Return(Core::ERROR_GENERAL));
    }

    void SetMS12ProfileList(const std::vector<std::string>& profiles)
    {
        ON_CALL(DeviceSettingsAudioMock::Mock(), GetAudioMS12ProfileList(::testing::_, ::testing::_))
            .WillByDefault(::testing::Invoke(
                [profiles](const int32_t, Exchange::IDeviceSettingsAudio::IDeviceSettingsAudioMS12AudioProfileIterator*& iter) -> Core::hresult {
                    std::list<Exchange::IDeviceSettingsAudio::MS12AudioProfile> list;
                    for (const auto& name : profiles) {
                        Exchange::IDeviceSettingsAudio::MS12AudioProfile entry;
                        entry.audioProfile = name;
                        list.emplace_back(entry);
                    }
                    iter = (Core::Service<RPC::IteratorType<Exchange::IDeviceSettingsAudio::IDeviceSettingsAudioMS12AudioProfileIterator>>::Create<Exchange::IDeviceSettingsAudio::IDeviceSettingsAudioMS12AudioProfileIterator>(list));
                    return Core::ERROR_NONE;
                }));
    }

    void SetMS12ProfileListError()
    {
        ON_CALL(DeviceSettingsAudioMock::Mock(), GetAudioMS12ProfileList(::testing::_, ::testing::_))
            .WillByDefault(::testing::Return(Core::ERROR_GENERAL));
    }
};

// =========== AudioCapabilities ===========

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Success_EmptyPort_AllCapabilities)
{
    SetAudioCapabilities(Exchange::IDeviceSettingsAudio::AUDIO_CAPS_ATMOS |
                          Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DOLBY_DIGITAL |
                          Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DOLBY_DIGITAL_PLUS |
                          Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DIGITAL_AUDIO_DELIVERY |
                          Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DIGITAL_AUDIO_PROCESS_V2 |
                          Exchange::IDeviceSettingsAudio::AUDIO_CAPS_MS12);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"ATMOS\"") != string::npos);
    EXPECT_TRUE(response.find("\"DOLBY_DIGITAL\"") != string::npos);
    EXPECT_TRUE(response.find("\"DOLBY_DIGITAL_PLUS\"") != string::npos);
    EXPECT_TRUE(response.find("\"Dual_Audio_Decode\"") != string::npos);
    EXPECT_TRUE(response.find("\"DAPv2\"") != string::npos);
    EXPECT_TRUE(response.find("\"MS12\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Success_SpecificPort_SingleCapability)
{
    SetAudioCapabilities(Exchange::IDeviceSettingsAudio::AUDIO_CAPS_ATMOS);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"SPDIF0\"}"), response));
    EXPECT_TRUE(response.find("\"ATMOS\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Success_NoCapabilities)
{
    SetAudioCapabilities(Exchange::IDeviceSettingsAudio::AUDIO_CAPS_NONE);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"none\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Success_AtmosOnly)
{
    SetAudioCapabilities(Exchange::IDeviceSettingsAudio::AUDIO_CAPS_ATMOS);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"ATMOS\"") != string::npos);
    EXPECT_FALSE(response.find("\"DOLBY_DIGITAL\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Success_DDandDDPlus)
{
    SetAudioCapabilities(Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DOLBY_DIGITAL |
                          Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DOLBY_DIGITAL_PLUS);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"SPEAKER0\"}"), response));
    EXPECT_TRUE(response.find("\"DOLBY_DIGITAL\"") != string::npos);
    EXPECT_TRUE(response.find("\"DOLBY_DIGITAL_PLUS\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Failure_DeviceException)
{
    // No HAL exception surface exists any more; the analogous failure at the new
    // COM-RPC boundary is GetAudioCapabilities() itself failing.
    SetAudioCapabilitiesError();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"\"}"), response));
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Failure_StdException)
{
    // Same underlying failure surface as *_Failure_DeviceException above.
    SetAudioCapabilitiesError();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"\"}"), response));
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Failure_UnknownException)
{
    // Same underlying failure surface as above.
    SetAudioCapabilitiesError();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"\"}"), response));
}

// =========== MS12Capabilities ===========

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Success_EmptyPort_AllCapabilities)
{
    SetMS12Capabilities(Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_DOLBYVOLUME |
                         Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_INTELLIGENT_EQUALIZER |
                         Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_DIALOG_ENHANCER);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"Dolby_Volume\"") != string::npos);
    EXPECT_TRUE(response.find("\"Inteligent_Equalizer\"") != string::npos);
    EXPECT_TRUE(response.find("\"Dialogue_Enhancer\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Success_SpecificPort_SingleCapability)
{
    SetMS12Capabilities(Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_DOLBYVOLUME);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI1\"}"), response));
    EXPECT_TRUE(response.find("\"Dolby_Volume\"") != string::npos);
    EXPECT_FALSE(response.find("\"Inteligent_Equalizer\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Success_NoCapabilities)
{
    SetMS12Capabilities(Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_NONE);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"none\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Success_DolbyVolumeOnly)
{
    SetMS12Capabilities(Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_DOLBYVOLUME);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"SPEAKER0\"}"), response));
    EXPECT_TRUE(response.find("\"Dolby_Volume\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Success_EqualizerAndEnhancer)
{
    SetMS12Capabilities(Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_INTELLIGENT_EQUALIZER |
                         Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_DIALOG_ENHANCER);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"Inteligent_Equalizer\"") != string::npos);
    EXPECT_TRUE(response.find("\"Dialogue_Enhancer\"") != string::npos);
    EXPECT_FALSE(response.find("\"Dolby_Volume\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Failure_DeviceException)
{
    // No HAL exception surface exists any more; the analogous failure at the new
    // COM-RPC boundary is GetAudioMS12Capabilities() itself failing.
    SetMS12CapabilitiesError();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"\"}"), response));
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Failure_StdException)
{
    // Same underlying failure surface as *_Failure_DeviceException above.
    SetMS12CapabilitiesError();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"\"}"), response));
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Failure_UnknownException)
{
    // Same underlying failure surface as above.
    SetMS12CapabilitiesError();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
}

// =========== SupportedMS12AudioProfiles ===========

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Success_EmptyPort_MultipleProfiles)
{
    SetMS12ProfileList({"Movie", "Music", "Voice"});

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"supportedMS12AudioProfiles\":[") != string::npos);
    EXPECT_TRUE(response.find("\"Movie\"") != string::npos);
    EXPECT_TRUE(response.find("\"Music\"") != string::npos);
    EXPECT_TRUE(response.find("\"Voice\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Success_SpecificPort_SingleProfile)
{
    SetMS12ProfileList({"Movie"});

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"SPDIF0\"}"), response));
    EXPECT_TRUE(response.find("\"Movie\"") != string::npos);
    EXPECT_FALSE(response.find("\"Music\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Success_EmptyList)
{
    SetMS12ProfileList({});

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Success_AllStandardProfiles)
{
    SetMS12ProfileList({"Movie", "Music", "Voice", "Sport", "Game"});

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"SPEAKER0\"}"), response));
    EXPECT_TRUE(response.find("\"Movie\"") != string::npos);
    EXPECT_TRUE(response.find("\"Music\"") != string::npos);
    EXPECT_TRUE(response.find("\"Voice\"") != string::npos);
    EXPECT_TRUE(response.find("\"Sport\"") != string::npos);
    EXPECT_TRUE(response.find("\"Game\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Failure_DeviceException)
{
    // No HAL exception surface exists any more; the analogous failure at the new
    // COM-RPC boundary is GetAudioMS12ProfileList() itself failing.
    SetMS12ProfileListError();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"\"}"), response));
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Failure_StdException)
{
    // Same underlying failure surface as *_Failure_DeviceException above.
    SetMS12ProfileListError();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"\"}"), response));
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Failure_UnknownException)
{
    // Same underlying failure surface as above.
    SetMS12ProfileListError();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"HDMI0\"}"), response));
}

// =========== Additional single-capability coverage ===========

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Success_DADCapability)
{
    SetAudioCapabilities(Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DIGITAL_AUDIO_DELIVERY);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Dual_Audio_Decode\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Success_DAPv2Capability)
{
    SetAudioCapabilities(Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DIGITAL_AUDIO_PROCESS_V2);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"DAPv2\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Success_MS12Capability)
{
    SetAudioCapabilities(Exchange::IDeviceSettingsAudio::AUDIO_CAPS_MS12);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"MS12\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Success_DialogueEnhancerOnly)
{
    SetMS12Capabilities(Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_DIALOG_ENHANCER);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Dialogue_Enhancer\"") != string::npos);
    EXPECT_FALSE(response.find("\"Dolby_Volume\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

// =========== Additional Negative Tests ===========

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Negative_InvalidAudioPort)
{
    // Unknown port: the refactored plugin reports NONE capability with success, not an error.
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"INVALID_PORT\"}"), response));
    EXPECT_TRUE(response.find("\"none\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Negative_GetDefaultAudioPortNameThrows)
{
    // No audio ports configured at all -> DSHelper::getAudioPortEntries() itself
    // fails -> Core::ERROR_UNAVAILABLE, distinct from "port not found" above.
    SetEmptyAudioPortConfig();

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Negative_GetAudioCapabilitiesThrows)
{
    SetAudioCapabilitiesError();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Negative_GetAudioOutputPortThrowsStd)
{
    // Same underlying failure surface as *_Negative_GetAudioCapabilitiesThrows above.
    SetAudioCapabilitiesError();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Negative_GetAudioCapabilitiesStdException)
{
    SetAudioCapabilitiesError();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Negative_UnknownExceptionInGetCapabilities)
{
    SetAudioCapabilitiesError();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Negative_EmptyPortGetInstanceThrows)
{
    SetEmptyAudioPortConfig();

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Negative_InvalidAudioPort)
{
    // Unknown port: the refactored plugin reports NONE capability with success, not an error.
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"INVALID_PORT\"}"), response));
    EXPECT_TRUE(response.find("\"none\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Negative_GetDefaultAudioPortNameThrows)
{
    SetEmptyAudioPortConfig();

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Negative_GetMS12CapabilitiesThrows)
{
    SetMS12CapabilitiesError();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Negative_GetAudioOutputPortThrowsStd)
{
    SetMS12CapabilitiesError();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"SPDIF0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Negative_GetMS12CapabilitiesStdException)
{
    SetMS12CapabilitiesError();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Negative_UnknownExceptionInGetCapabilities)
{
    SetMS12CapabilitiesError();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Negative_EmptyPortGetInstanceThrows)
{
    SetEmptyAudioPortConfig();

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Negative_InvalidAudioPort)
{
    // Unknown port: the refactored plugin returns an empty profile list with success.
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"INVALID_PORT\"}"), response));
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Negative_GetDefaultAudioPortNameThrows)
{
    SetEmptyAudioPortConfig();

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Negative_GetMS12AudioProfileListThrows)
{
    SetMS12ProfileListError();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Negative_GetAudioOutputPortThrowsStd)
{
    SetMS12ProfileListError();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"SPDIF0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Negative_GetMS12AudioProfileListStdException)
{
    SetMS12ProfileListError();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Negative_UnknownExceptionInGetProfileList)
{
    SetMS12ProfileListError();

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Negative_EmptyPortGetInstanceThrows)
{
    SetEmptyAudioPortConfig();

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Negative_ProfileListAtThrows)
{
    SetMS12ProfileList({"Movie"});

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Movie\"") != string::npos);
}

// =========== Additional Comprehensive Positive Tests ===========

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Positive_VariousPortNames)
{
    SetAudioCapabilities(Exchange::IDeviceSettingsAudio::AUDIO_CAPS_ATMOS);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"ATMOS\"") != string::npos);

    SetAudioCapabilities(Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DOLBY_DIGITAL);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI1\"}"), response));
    EXPECT_TRUE(response.find("\"DOLBY_DIGITAL\"") != string::npos);

    SetAudioCapabilities(Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DOLBY_DIGITAL_PLUS);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"SPDIF0\"}"), response));
    EXPECT_TRUE(response.find("\"DOLBY_DIGITAL_PLUS\"") != string::npos);

    SetAudioCapabilities(Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DIGITAL_AUDIO_DELIVERY);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"SPEAKER0\"}"), response));
    EXPECT_TRUE(response.find("\"Dual_Audio_Decode\"") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Positive_AllIndividualCapabilities)
{
    static const int32_t kCaps[] = {
        Exchange::IDeviceSettingsAudio::AUDIO_CAPS_ATMOS,
        Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DOLBY_DIGITAL,
        Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DOLBY_DIGITAL_PLUS,
        Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DIGITAL_AUDIO_DELIVERY,
        Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DIGITAL_AUDIO_PROCESS_V2,
        Exchange::IDeviceSettingsAudio::AUDIO_CAPS_MS12,
    };
    static const char* const kExpected[] = {
        "\"ATMOS\"", "\"DOLBY_DIGITAL\"", "\"DOLBY_DIGITAL_PLUS\"",
        "\"Dual_Audio_Decode\"", "\"DAPv2\"", "\"MS12\"",
    };
    for (size_t i = 0; i < sizeof(kCaps) / sizeof(kCaps[0]); ++i) {
        SetAudioCapabilities(kCaps[i]);
        EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
        EXPECT_TRUE(response.find(kExpected[i]) != string::npos);
    }
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Positive_CombinationSubsets)
{
    SetAudioCapabilities(Exchange::IDeviceSettingsAudio::AUDIO_CAPS_ATMOS | Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DOLBY_DIGITAL);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"ATMOS\"") != string::npos);
    EXPECT_TRUE(response.find("\"DOLBY_DIGITAL\"") != string::npos);
    EXPECT_FALSE(response.find("\"DOLBY_DIGITAL_PLUS\"") != string::npos);

    SetAudioCapabilities(Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DOLBY_DIGITAL |
                          Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DOLBY_DIGITAL_PLUS |
                          Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DIGITAL_AUDIO_DELIVERY);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"DOLBY_DIGITAL\"") != string::npos);
    EXPECT_TRUE(response.find("\"DOLBY_DIGITAL_PLUS\"") != string::npos);
    EXPECT_TRUE(response.find("\"Dual_Audio_Decode\"") != string::npos);

    SetAudioCapabilities(Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DIGITAL_AUDIO_PROCESS_V2 | Exchange::IDeviceSettingsAudio::AUDIO_CAPS_MS12);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"DAPv2\"") != string::npos);
    EXPECT_TRUE(response.find("\"MS12\"") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Positive_DefaultPortUsed)
{
    // Empty audioPort resolves to the first configured entry — the fixture's
    // default config starts with HDMI0.
    SetAudioCapabilities(Exchange::IDeviceSettingsAudio::AUDIO_CAPS_ATMOS);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"ATMOS\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Positive_AllIndividualCapabilities)
{
    SetMS12Capabilities(Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_DOLBYVOLUME);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Dolby_Volume\"") != string::npos);
    EXPECT_FALSE(response.find("\"Inteligent_Equalizer\"") != string::npos);

    SetMS12Capabilities(Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_INTELLIGENT_EQUALIZER);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Inteligent_Equalizer\"") != string::npos);
    EXPECT_FALSE(response.find("\"Dolby_Volume\"") != string::npos);

    SetMS12Capabilities(Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_DIALOG_ENHANCER);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Dialogue_Enhancer\"") != string::npos);
    EXPECT_FALSE(response.find("\"Dolby_Volume\"") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Positive_CombinationSubsets)
{
    SetMS12Capabilities(Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_DOLBYVOLUME | Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_INTELLIGENT_EQUALIZER);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Dolby_Volume\"") != string::npos);
    EXPECT_TRUE(response.find("\"Inteligent_Equalizer\"") != string::npos);
    EXPECT_FALSE(response.find("\"Dialogue_Enhancer\"") != string::npos);

    SetMS12Capabilities(Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_INTELLIGENT_EQUALIZER | Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_DIALOG_ENHANCER);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Inteligent_Equalizer\"") != string::npos);
    EXPECT_TRUE(response.find("\"Dialogue_Enhancer\"") != string::npos);
    EXPECT_FALSE(response.find("\"Dolby_Volume\"") != string::npos);

    SetMS12Capabilities(Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_DOLBYVOLUME | Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_DIALOG_ENHANCER);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Dolby_Volume\"") != string::npos);
    EXPECT_TRUE(response.find("\"Dialogue_Enhancer\"") != string::npos);
    EXPECT_FALSE(response.find("\"Inteligent_Equalizer\"") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Positive_VariousPortNames)
{
    SetMS12Capabilities(Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_DOLBYVOLUME);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Dolby_Volume\"") != string::npos);

    SetMS12Capabilities(Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_INTELLIGENT_EQUALIZER);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"SPDIF0\"}"), response));
    EXPECT_TRUE(response.find("\"Inteligent_Equalizer\"") != string::npos);

    SetMS12Capabilities(Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_DIALOG_ENHANCER);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"SPEAKER0\"}"), response));
    EXPECT_TRUE(response.find("\"Dialogue_Enhancer\"") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Positive_DefaultPortUsed)
{
    // Empty audioPort resolves to the first configured entry (HDMI0 by default).
    SetMS12Capabilities(Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_DOLBYVOLUME);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"Dolby_Volume\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Positive_VariousProfiles)
{
    SetMS12ProfileList({"Movie", "Music"});
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Movie\"") != string::npos);
    EXPECT_TRUE(response.find("\"Music\"") != string::npos);
    EXPECT_FALSE(response.find("\"Voice\"") != string::npos);

    SetMS12ProfileList({"Movie", "Music", "Voice", "Sport"});
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Movie\"") != string::npos);
    EXPECT_TRUE(response.find("\"Music\"") != string::npos);
    EXPECT_TRUE(response.find("\"Voice\"") != string::npos);
    EXPECT_TRUE(response.find("\"Sport\"") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Positive_LargeProfileList)
{
    const std::vector<std::string> profiles = {
        "Movie", "Music", "Voice", "Sport", "Game",
        "Night", "Standard", "Custom1", "Custom2", "Custom3"
    };
    SetMS12ProfileList(profiles);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"HDMI0\"}"), response));

    for (const auto& profile : profiles) {
        EXPECT_TRUE(response.find(profile) != string::npos);
    }
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Positive_DefaultPortUsed)
{
    // Only SPEAKER0 configured (no HDMI0) -> DSHelper::getDefaultAudioPortName()
    // falls back to it.
    SetAudioPortConfig({
        { Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_SPEAKER, 0 },
    });
    SetMS12ProfileList({"Movie", "Music"});

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"Movie\"") != string::npos);
    EXPECT_TRUE(response.find("\"Music\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Positive_VariousPortNames)
{
    SetMS12ProfileList({"Movie"});
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Movie\"") != string::npos);

    SetMS12ProfileList({"Music", "Voice"});
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"SPDIF0\"}"), response));
    EXPECT_TRUE(response.find("\"Music\"") != string::npos);
    EXPECT_TRUE(response.find("\"Voice\"") != string::npos);

    SetMS12ProfileList({"Sport", "Game", "Night"});
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"SPEAKER0\"}"), response));
    EXPECT_TRUE(response.find("\"Sport\"") != string::npos);
    EXPECT_TRUE(response.find("\"Game\"") != string::npos);
    EXPECT_TRUE(response.find("\"Night\"") != string::npos);
}

// =========== Boundary and Edge Case Tests ===========

TEST_F(DeviceAudioCapabilitiesTest, Boundary_AudioCapabilities_ZeroCapabilities)
{
    SetAudioCapabilities(0);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"none\"") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, Boundary_AudioCapabilities_MaxCombination)
{
    SetAudioCapabilities(Exchange::IDeviceSettingsAudio::AUDIO_CAPS_ATMOS |
                          Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DOLBY_DIGITAL |
                          Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DOLBY_DIGITAL_PLUS |
                          Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DIGITAL_AUDIO_DELIVERY |
                          Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DIGITAL_AUDIO_PROCESS_V2 |
                          Exchange::IDeviceSettingsAudio::AUDIO_CAPS_MS12);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"ATMOS\"") != string::npos);
    EXPECT_TRUE(response.find("\"DOLBY_DIGITAL\"") != string::npos);
    EXPECT_TRUE(response.find("\"DOLBY_DIGITAL_PLUS\"") != string::npos);
    EXPECT_TRUE(response.find("\"Dual_Audio_Decode\"") != string::npos);
    EXPECT_TRUE(response.find("\"DAPv2\"") != string::npos);
    EXPECT_TRUE(response.find("\"MS12\"") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, Boundary_MS12Capabilities_ZeroCapabilities)
{
    SetMS12Capabilities(0);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"none\"") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, Boundary_MS12Capabilities_AllCapabilities)
{
    SetMS12Capabilities(Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_DOLBYVOLUME |
                         Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_INTELLIGENT_EQUALIZER |
                         Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_DIALOG_ENHANCER);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Dolby_Volume\"") != string::npos);
    EXPECT_TRUE(response.find("\"Inteligent_Equalizer\"") != string::npos);
    EXPECT_TRUE(response.find("\"Dialogue_Enhancer\"") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, Boundary_SupportedMS12AudioProfiles_SingleProfile)
{
    SetMS12ProfileList({"SingleProfile"});

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"SingleProfile\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, EdgeCase_SequentialCallsSamePort)
{
    SetAudioCapabilities(Exchange::IDeviceSettingsAudio::AUDIO_CAPS_ATMOS);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"ATMOS\"") != string::npos);

    SetMS12Capabilities(Exchange::IDeviceSettingsAudio::AUDIO_MS12_CAPABILITIES_DOLBYVOLUME);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Dolby_Volume\"") != string::npos);

    SetMS12ProfileList({"Movie"});
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Movie\"") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, EdgeCase_AlternatingPortCalls)
{
    SetAudioCapabilities(Exchange::IDeviceSettingsAudio::AUDIO_CAPS_ATMOS);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));

    SetAudioCapabilities(Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DOLBY_DIGITAL);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"SPDIF0\"}"), response));

    SetAudioCapabilities(Exchange::IDeviceSettingsAudio::AUDIO_CAPS_DOLBY_DIGITAL_PLUS);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
}
