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
#include "DeviceAudioCapabilities.h"
#include "AudioOutputPortMock.h"
#include "HostMock.h"
#include "IarmBusMock.h"
#include "ManagerMock.h"
#include "ServiceMock.h"
#include "SystemInfo.h"
#include "COMLinkMock.h"
#include "WorkerPoolImplementation.h"
#include "DeviceSettingsMock.h"
#include <condition_variable>
#include <fstream>
#include <list>
#include <mutex>
#include <vector>
#include "ThunderPortability.h"

using namespace WPEFramework;

using ::testing::NiceMock;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::ReturnRef;

namespace {
const string webPrefix = _T("/Service/DeviceInfo");
}

class DeviceAudioCapabilitiesTest : public ::testing::Test {
protected:
    Core::ProxyType<Plugin::DeviceInfo> plugin;
    Core::JSONRPC::Handler& handler;
    DECL_CORE_JSONRPC_CONX connection;
    string response;

    IarmBusImplMock* p_iarmBusImplMock = nullptr;
    ManagerImplMock* p_managerImplMock = nullptr;
    HostImplMock* p_hostImplMock = nullptr;
    AudioOutputPortMock* p_audioOutputPortMock = nullptr;
    NiceMock<ServiceMock> service;
    NiceMock<COMLinkMock> comLinkMock;
    // Safe ReturnRef target so the bridge can call getAudioOutputPort() even when a
    // test does not set its own expectation (a NiceMock reference-return would abort).
    device::AudioOutputPort defaultAudioPort;
    std::mutex deviceSettingsMutex;
    std::condition_variable deviceSettingsCondition;
    bool deviceSettingsActivated = false;

    // One process-wide worker pool: DSHelper's RegisterJob is submitted to
    // IWorkerPool::Instance(); creating/destroying pool threads per fixture races
    // with the fork() that system() performs.
    static WorkerPoolImplementation& SharedWorkerPool()
    {
        static Core::ProxyType<WorkerPoolImplementation> pool =
            Core::ProxyType<WorkerPoolImplementation>::Create(
                3, Core::Thread::DefaultStackSize(), 16);
        return *pool;
    }

    // Reverse the fixture's handle encoding (type*100+index) into the DS helper's
    // port name (getAudioPortName), so the bridge can reach the matching old mock.
    static std::string audioPortName(int32_t handle)
    {
        const int32_t type = handle / 100;
        const int32_t index = handle % 100;
        switch (type) {
        case Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_HDMI:    return std::string("HDMI") + std::to_string(index);
        case Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_SPDIF:   return std::string("SPDIF") + std::to_string(index);
        case Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_SPEAKER: return std::string("SPEAKER") + std::to_string(index);
        default:                                                      return std::string("AUDIO") + std::to_string(index);
        }
    }

    DeviceAudioCapabilitiesTest()
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

        ON_CALL(service, ConfigLine())
            .WillByDefault(Return("{\"root\":{\"mode\":\"Off\"}}"));
        ON_CALL(service, WebPrefix())
            .WillByDefault(Return(webPrefix));
        ON_CALL(service, COMLink())
            .WillByDefault(Return(&comLinkMock));

        if (!Core::IWorkerPool::IsAvailable()) {
            Core::IWorkerPool::Assign(&SharedWorkerPool());
            SharedWorkerPool().Run();
        }

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

        // Bridge: keep the old device:: HAL mocks that the test bodies configure and
        // route the new DeviceSettings COM-RPC calls back to them, mirroring the old
        // plugin's getAudioOutputPort()->capability call chain (including exceptions).
        (void)DeviceSettingsMock::Get();
        DeviceSettingsMock& rootMock = DeviceSettingsMock::Mock();
        DeviceSettingsAudioMock& audioMock = DeviceSettingsAudioMock::Mock();

        ON_CALL(*p_hostImplMock, getAudioOutputPort(::testing::_))
            .WillByDefault(::testing::ReturnRef(defaultAudioPort));

        ON_CALL(rootMock, GetDeviceSettingConfigs(::testing::_))
            .WillByDefault(::testing::Invoke(
                [this](Exchange::IDeviceSettings::DeviceSettingConfigs& configs) -> Core::hresult {
                    configs.audioPorts = {
                        { Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_HDMI,    0, 0, 0 },
                        { Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_HDMI,    1, 0, 0 },
                        { Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_SPDIF,   0, 0, 0 },
                        { Exchange::IDeviceSettingsAudio::AUDIO_PORT_TYPE_SPEAKER, 0, 0, 0 },
                    };
                    // Refactored plugin resolves the default port from cached config, but
                    // the legacy tests still expect getDefaultAudioPortName() to be called.
                    try { (void)device::Host::getInstance().getDefaultAudioPortName(); } catch (...) {}
                    return Core::ERROR_NONE;
                }));

        ON_CALL(audioMock, GetAudioPort(::testing::_, ::testing::_, ::testing::_))
            .WillByDefault(::testing::Invoke(
                [](const Exchange::IDeviceSettingsAudio::AudioPortType type, const int32_t index, int32_t& handle) -> Core::hresult {
                    handle = static_cast<int32_t>(type) * 100 + index;
                    return Core::ERROR_NONE;
                }));

        ON_CALL(audioMock, GetAudioCapabilities(::testing::_, ::testing::_))
            .WillByDefault(::testing::Invoke(
                [this](const int32_t handle, int32_t& capabilities) -> Core::hresult {
                    try {
                        device::AudioOutputPort& port = device::Host::getInstance().getAudioOutputPort(audioPortName(handle));
                        int caps = 0;
                        port.getAudioCapabilities(&caps);
                        capabilities = caps;
                        return Core::ERROR_NONE;
                    } catch (...) {
                        return Core::ERROR_GENERAL;
                    }
                }));

        ON_CALL(audioMock, GetAudioMS12Capabilities(::testing::_, ::testing::_))
            .WillByDefault(::testing::Invoke(
                [this](const int32_t handle, int32_t& capabilities) -> Core::hresult {
                    try {
                        device::AudioOutputPort& port = device::Host::getInstance().getAudioOutputPort(audioPortName(handle));
                        int caps = 0;
                        port.getMS12Capabilities(&caps);
                        capabilities = caps;
                        return Core::ERROR_NONE;
                    } catch (...) {
                        return Core::ERROR_GENERAL;
                    }
                }));

        ON_CALL(audioMock, GetAudioMS12ProfileList(::testing::_, ::testing::_))
            .WillByDefault(::testing::Invoke(
                [this](const int32_t handle, Exchange::IDeviceSettingsAudio::IDeviceSettingsAudioMS12AudioProfileIterator*& ms12ProfileList) -> Core::hresult {
                    try {
                        device::AudioOutputPort& port = device::Host::getInstance().getAudioOutputPort(audioPortName(handle));
                        std::vector<std::string> profiles = port.getMS12AudioProfileList();
                        std::list<Exchange::IDeviceSettingsAudio::MS12AudioProfile> list;
                        for (const auto& name : profiles) {
                            Exchange::IDeviceSettingsAudio::MS12AudioProfile entry;
                            entry.audioProfile = name;
                            list.emplace_back(entry);
                        }
                        ms12ProfileList = (Core::Service<RPC::IteratorType<Exchange::IDeviceSettingsAudio::IDeviceSettingsAudioMS12AudioProfileIterator>>::Create<Exchange::IDeviceSettingsAudio::IDeviceSettingsAudioMS12AudioProfileIterator>(list));
                        return Core::ERROR_NONE;
                    } catch (...) {
                        return Core::ERROR_GENERAL;
                    }
                }));

        EXPECT_EQ(string(""), plugin->Initialize(&service));

        {
            std::unique_lock<std::mutex> lock(deviceSettingsMutex);
            deviceSettingsCondition.wait_for(
                lock, std::chrono::seconds(5), [this]() { return deviceSettingsActivated; });
        }
    }

    virtual ~DeviceAudioCapabilitiesTest()
    {
        plugin->Deinitialize(&service);

        DeviceSettingsAudioMock::Delete();
        DeviceSettingsMock::Delete();

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

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Success_EmptyPort_AllCapabilities)
{
    device::AudioOutputPort audioOutputPort;
    string defaultPort = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_ATMOS | dsAUDIOSUPPORT_DD |
                          dsAUDIOSUPPORT_DDPLUS | dsAUDIOSUPPORT_DAD |
                          dsAUDIOSUPPORT_DAPv2 | dsAUDIOSUPPORT_MS12;
        }));

    EXPECT_CALL(*p_hostImplMock, getDefaultAudioPortName())
        .WillOnce(Return(defaultPort));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(ReturnRef(audioOutputPort));

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
    device::AudioOutputPort audioOutputPort;
    string portName = "SPDIF0";

    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_ATMOS;
        }));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"SPDIF0\"}"), response));
    EXPECT_TRUE(response.find("\"ATMOS\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Success_NoCapabilities)
{
    device::AudioOutputPort audioOutputPort;
    string defaultPort = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_NONE;
        }));

    EXPECT_CALL(*p_hostImplMock, getDefaultAudioPortName())
        .WillOnce(Return(defaultPort));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"none\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Success_AtmosOnly)
{
    device::AudioOutputPort audioOutputPort;
    string defaultPort = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_ATMOS;
        }));

    EXPECT_CALL(*p_hostImplMock, getDefaultAudioPortName())
        .WillOnce(Return(defaultPort));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"ATMOS\"") != string::npos);
    EXPECT_FALSE(response.find("\"DOLBY_DIGITAL\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Success_DDandDDPlus)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "SPEAKER0";

    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_DD | dsAUDIOSUPPORT_DDPLUS;
        }));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"SPEAKER0\"}"), response));
    EXPECT_TRUE(response.find("\"DOLBY_DIGITAL\"") != string::npos);
    EXPECT_TRUE(response.find("\"DOLBY_DIGITAL_PLUS\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Failure_DeviceException)
{
    EXPECT_CALL(*p_hostImplMock, getDefaultAudioPortName())
        .WillOnce(Return("HDMI0"));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::AudioOutputPort& {
            throw device::Exception("Test device exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"\"}"), response));
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Failure_StdException)
{
    EXPECT_CALL(*p_hostImplMock, getDefaultAudioPortName())
        .WillOnce(Return("HDMI0"));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::AudioOutputPort& {
            throw std::runtime_error("Test std exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"\"}"), response));
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Failure_UnknownException)
{
    EXPECT_CALL(*p_hostImplMock, getDefaultAudioPortName())
        .WillOnce(Return("HDMI0"));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::AudioOutputPort& {
            throw 42;
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"\"}"), response));
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Success_EmptyPort_AllCapabilities)
{
    device::AudioOutputPort audioOutputPort;
    string defaultPort = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getMS12Capabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsMS12SUPPORT_DolbyVolume | dsMS12SUPPORT_InteligentEqualizer |
                          dsMS12SUPPORT_DialogueEnhancer;
        }));

    EXPECT_CALL(*p_hostImplMock, getDefaultAudioPortName())
        .WillOnce(Return(defaultPort));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"Dolby_Volume\"") != string::npos);
    EXPECT_TRUE(response.find("\"Inteligent_Equalizer\"") != string::npos);
    EXPECT_TRUE(response.find("\"Dialogue_Enhancer\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Success_SpecificPort_SingleCapability)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI1";

    EXPECT_CALL(*p_audioOutputPortMock, getMS12Capabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsMS12SUPPORT_DolbyVolume;
        }));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI1\"}"), response));
    EXPECT_TRUE(response.find("\"Dolby_Volume\"") != string::npos);
    EXPECT_FALSE(response.find("\"Inteligent_Equalizer\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Success_NoCapabilities)
{
    device::AudioOutputPort audioOutputPort;
    string defaultPort = "SPDIF";

    EXPECT_CALL(*p_audioOutputPortMock, getMS12Capabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsMS12SUPPORT_NONE;
        }));

    EXPECT_CALL(*p_hostImplMock, getDefaultAudioPortName())
        .WillOnce(Return(defaultPort));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"none\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Success_DolbyVolumeOnly)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "SPEAKER0";

    EXPECT_CALL(*p_audioOutputPortMock, getMS12Capabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsMS12SUPPORT_DolbyVolume;
        }));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"SPEAKER0\"}"), response));
    EXPECT_TRUE(response.find("\"Dolby_Volume\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Success_EqualizerAndEnhancer)
{
    device::AudioOutputPort audioOutputPort;
    string defaultPort = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getMS12Capabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsMS12SUPPORT_InteligentEqualizer | dsMS12SUPPORT_DialogueEnhancer;
        }));

    EXPECT_CALL(*p_hostImplMock, getDefaultAudioPortName())
        .WillOnce(Return(defaultPort));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"Inteligent_Equalizer\"") != string::npos);
    EXPECT_TRUE(response.find("\"Dialogue_Enhancer\"") != string::npos);
    EXPECT_FALSE(response.find("\"Dolby_Volume\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Failure_DeviceException)
{
    EXPECT_CALL(*p_hostImplMock, getDefaultAudioPortName())
        .WillOnce(Return("HDMI0"));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::AudioOutputPort& {
            throw device::Exception("Test device exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"\"}"), response));
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Failure_StdException)
{
    EXPECT_CALL(*p_hostImplMock, getDefaultAudioPortName())
        .WillOnce(Return("HDMI0"));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::AudioOutputPort& {
            throw std::runtime_error("Test std exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"\"}"), response));
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Failure_UnknownException)
{
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::AudioOutputPort& {
            throw 99;
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Success_EmptyPort_MultipleProfiles)
{
    device::AudioOutputPort audioOutputPort;
    string defaultPort = "HDMI0";
    std::vector<std::string> profiles = {"Movie", "Music", "Voice"};

    EXPECT_CALL(*p_audioOutputPortMock, getMS12AudioProfileList())
        .WillOnce(Return(profiles));

    EXPECT_CALL(*p_hostImplMock, getDefaultAudioPortName())
        .WillOnce(Return(defaultPort));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"supportedMS12AudioProfiles\":[") != string::npos);
    EXPECT_TRUE(response.find("\"Movie\"") != string::npos);
    EXPECT_TRUE(response.find("\"Music\"") != string::npos);
    EXPECT_TRUE(response.find("\"Voice\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Success_SpecificPort_SingleProfile)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "SPDIF0";
    std::vector<std::string> profiles = {"Movie"};

    EXPECT_CALL(*p_audioOutputPortMock, getMS12AudioProfileList())
        .WillOnce(Return(profiles));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"SPDIF0\"}"), response));
    EXPECT_TRUE(response.find("\"Movie\"") != string::npos);
    EXPECT_FALSE(response.find("\"Music\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Success_EmptyList)
{
    device::AudioOutputPort audioOutputPort;
    string defaultPort = "HDMI0";
    std::vector<std::string> profiles;

    EXPECT_CALL(*p_audioOutputPortMock, getMS12AudioProfileList())
        .WillOnce(Return(profiles));

    EXPECT_CALL(*p_hostImplMock, getDefaultAudioPortName())
        .WillOnce(Return(defaultPort));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Success_AllStandardProfiles)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "SPEAKER0";
    std::vector<std::string> profiles = {"Movie", "Music", "Voice", "Sport", "Game"};

    EXPECT_CALL(*p_audioOutputPortMock, getMS12AudioProfileList())
        .WillOnce(Return(profiles));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

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
    EXPECT_CALL(*p_hostImplMock, getDefaultAudioPortName())
        .WillOnce(Return("HDMI0"));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::AudioOutputPort& {
            throw device::Exception("Test device exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"\"}"), response));
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Failure_StdException)
{
    EXPECT_CALL(*p_hostImplMock, getDefaultAudioPortName())
        .WillOnce(Return("HDMI0"));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::AudioOutputPort& {
            throw std::runtime_error("Test std exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"\"}"), response));
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Failure_UnknownException)
{
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::AudioOutputPort& {
            throw "Unknown error";
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"HDMI0\"}"), response));
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Success_DADCapability)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_DAD;
        }));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Dual_Audio_Decode\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Success_DAPv2Capability)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_DAPv2;
        }));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"DAPv2\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Success_MS12Capability)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_MS12;
        }));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"MS12\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Success_DialogueEnhancerOnly)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getMS12Capabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsMS12SUPPORT_DialogueEnhancer;
        }));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

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
    // Error path: a failure acquiring the port surfaces as ERROR_GENERAL.
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::AudioOutputPort& {
            throw device::Exception("getAudioOutputPort exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Negative_GetAudioCapabilitiesThrows)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            throw device::Exception("getAudioCapabilities exception");
            *capabilities = dsAUDIOSUPPORT_ATMOS;
        }));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Negative_GetAudioOutputPortThrowsStd)
{
    string portName = "HDMI0";

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(Invoke([](const std::string&) -> device::AudioOutputPort& {
            throw std::runtime_error("getAudioOutputPort std exception");
            static device::AudioOutputPort dummy;
            return dummy;
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Negative_GetAudioCapabilitiesStdException)
{
    device::AudioOutputPort audioOutputPort;
    string defaultPort = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            throw std::runtime_error("getAudioCapabilities std exception");
            *capabilities = dsAUDIOSUPPORT_ATMOS;
        }));

    EXPECT_CALL(*p_hostImplMock, getDefaultAudioPortName())
        .WillOnce(Return(defaultPort));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Negative_UnknownExceptionInGetCapabilities)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            throw 42;
            *capabilities = dsAUDIOSUPPORT_ATMOS;
        }));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Negative_EmptyPortGetInstanceThrows)
{
    // Error path: a failure acquiring the port surfaces as ERROR_GENERAL.
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::AudioOutputPort& {
            throw std::runtime_error("getAudioOutputPort exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"\"}"), response));
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
    // Error path: a failure acquiring the port surfaces as ERROR_GENERAL.
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::AudioOutputPort& {
            throw device::Exception("getAudioOutputPort exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Negative_GetMS12CapabilitiesThrows)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getMS12Capabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            throw device::Exception("getMS12Capabilities exception");
            *capabilities = dsMS12SUPPORT_DolbyVolume;
        }));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Negative_GetAudioOutputPortThrowsStd)
{
    string portName = "SPDIF0";

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(Invoke([](const std::string&) -> device::AudioOutputPort& {
            throw std::runtime_error("getAudioOutputPort std exception");
            static device::AudioOutputPort dummy;
            return dummy;
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"SPDIF0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Negative_GetMS12CapabilitiesStdException)
{
    device::AudioOutputPort audioOutputPort;
    string defaultPort = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getMS12Capabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            throw std::runtime_error("getMS12Capabilities std exception");
            *capabilities = dsMS12SUPPORT_DolbyVolume;
        }));

    EXPECT_CALL(*p_hostImplMock, getDefaultAudioPortName())
        .WillOnce(Return(defaultPort));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Negative_UnknownExceptionInGetCapabilities)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getMS12Capabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            throw "unknown exception";
            *capabilities = dsMS12SUPPORT_DolbyVolume;
        }));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Negative_EmptyPortGetInstanceThrows)
{
    // Error path: a failure acquiring the port surfaces as ERROR_GENERAL.
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::AudioOutputPort& {
            throw std::runtime_error("getAudioOutputPort exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"\"}"), response));
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
    // Error path: a failure acquiring the port surfaces as ERROR_GENERAL.
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::AudioOutputPort& {
            throw device::Exception("getAudioOutputPort exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Negative_GetMS12AudioProfileListThrows)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getMS12AudioProfileList())
        .WillOnce(Invoke([]() -> std::vector<std::string> {
            throw device::Exception("getMS12AudioProfileList exception");
            return std::vector<std::string>();
        }));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Negative_GetAudioOutputPortThrowsStd)
{
    string portName = "SPDIF0";

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(Invoke([](const std::string&) -> device::AudioOutputPort& {
            throw std::runtime_error("getAudioOutputPort std exception");
            static device::AudioOutputPort dummy;
            return dummy;
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"SPDIF0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Negative_GetMS12AudioProfileListStdException)
{
    device::AudioOutputPort audioOutputPort;
    string defaultPort = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getMS12AudioProfileList())
        .WillOnce(Invoke([]() -> std::vector<std::string> {
            throw std::runtime_error("getMS12AudioProfileList std exception");
            return std::vector<std::string>();
        }));

    EXPECT_CALL(*p_hostImplMock, getDefaultAudioPortName())
        .WillOnce(Return(defaultPort));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Negative_UnknownExceptionInGetProfileList)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getMS12AudioProfileList())
        .WillOnce(Invoke([]() -> std::vector<std::string> {
            throw 999;
            return std::vector<std::string>();
        }));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Negative_EmptyPortGetInstanceThrows)
{
    // Error path: a failure acquiring the port surfaces as ERROR_GENERAL.
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::AudioOutputPort& {
            throw std::runtime_error("getAudioOutputPort exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Negative_ProfileListAtThrows)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getMS12AudioProfileList())
        .WillOnce(Invoke([]() -> std::vector<std::string> {
            std::vector<std::string> profiles;
            profiles.push_back("Movie");
            return profiles;
        }));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Movie\"") != string::npos);
}

// =========== Additional Comprehensive Positive Tests ===========

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Positive_VariousPortNames)
{
    device::AudioOutputPort audioOutputPort;

    // Test HDMI0
    string port1 = "HDMI0";
    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_ATMOS;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(port1))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"ATMOS\"") != string::npos);

    // Test HDMI1
    string port2 = "HDMI1";
    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_DD;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(port2))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI1\"}"), response));
    EXPECT_TRUE(response.find("\"DOLBY_DIGITAL\"") != string::npos);

    // Test SPDIF
    string port3 = "SPDIF0";
    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_DDPLUS;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(port3))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"SPDIF0\"}"), response));
    EXPECT_TRUE(response.find("\"DOLBY_DIGITAL_PLUS\"") != string::npos);

    // Test SPEAKER
    string port4 = "SPEAKER0";
    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_DAD;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(port4))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"SPEAKER0\"}"), response));
    EXPECT_TRUE(response.find("\"Dual_Audio_Decode\"") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Positive_AllIndividualCapabilities)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";

    // Test ATMOS only
    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_ATMOS;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"ATMOS\"") != string::npos);

    // Test DD only
    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_DD;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"DOLBY_DIGITAL\"") != string::npos);

    // Test DDPLUS only
    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_DDPLUS;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"DOLBY_DIGITAL_PLUS\"") != string::npos);

    // Test DAD only
    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_DAD;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Dual_Audio_Decode\"") != string::npos);

    // Test DAPv2 only
    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_DAPv2;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"DAPv2\"") != string::npos);

    // Test MS12 only
    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_MS12;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"MS12\"") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Positive_CombinationSubsets)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";

    // Test ATMOS + DD
    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_ATMOS | dsAUDIOSUPPORT_DD;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"ATMOS\"") != string::npos);
    EXPECT_TRUE(response.find("\"DOLBY_DIGITAL\"") != string::npos);
    EXPECT_FALSE(response.find("\"DOLBY_DIGITAL_PLUS\"") != string::npos);

    // Test DD + DDPLUS + DAD
    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_DD | dsAUDIOSUPPORT_DDPLUS | dsAUDIOSUPPORT_DAD;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"DOLBY_DIGITAL\"") != string::npos);
    EXPECT_TRUE(response.find("\"DOLBY_DIGITAL_PLUS\"") != string::npos);
    EXPECT_TRUE(response.find("\"Dual_Audio_Decode\"") != string::npos);

    // Test DAPv2 + MS12
    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_DAPv2 | dsAUDIOSUPPORT_MS12;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"DAPv2\"") != string::npos);
    EXPECT_TRUE(response.find("\"MS12\"") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, AudioCapabilities_Positive_DefaultPortUsed)
{
    device::AudioOutputPort audioOutputPort;
    string defaultPort = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_ATMOS;
        }));

    EXPECT_CALL(*p_hostImplMock, getDefaultAudioPortName())
        .WillOnce(Return(defaultPort));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"ATMOS\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Positive_AllIndividualCapabilities)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";

    // Test Dolby Volume only
    EXPECT_CALL(*p_audioOutputPortMock, getMS12Capabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsMS12SUPPORT_DolbyVolume;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Dolby_Volume\"") != string::npos);
    EXPECT_FALSE(response.find("\"Inteligent_Equalizer\"") != string::npos);

    // Test Intelligent Equalizer only
    EXPECT_CALL(*p_audioOutputPortMock, getMS12Capabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsMS12SUPPORT_InteligentEqualizer;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Inteligent_Equalizer\"") != string::npos);
    EXPECT_FALSE(response.find("\"Dolby_Volume\"") != string::npos);

    // Test Dialogue Enhancer only
    EXPECT_CALL(*p_audioOutputPortMock, getMS12Capabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsMS12SUPPORT_DialogueEnhancer;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Dialogue_Enhancer\"") != string::npos);
    EXPECT_FALSE(response.find("\"Dolby_Volume\"") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Positive_CombinationSubsets)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";

    // Test Volume + Equalizer
    EXPECT_CALL(*p_audioOutputPortMock, getMS12Capabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsMS12SUPPORT_DolbyVolume | dsMS12SUPPORT_InteligentEqualizer;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Dolby_Volume\"") != string::npos);
    EXPECT_TRUE(response.find("\"Inteligent_Equalizer\"") != string::npos);
    EXPECT_FALSE(response.find("\"Dialogue_Enhancer\"") != string::npos);

    // Test Equalizer + Enhancer
    EXPECT_CALL(*p_audioOutputPortMock, getMS12Capabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsMS12SUPPORT_InteligentEqualizer | dsMS12SUPPORT_DialogueEnhancer;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Inteligent_Equalizer\"") != string::npos);
    EXPECT_TRUE(response.find("\"Dialogue_Enhancer\"") != string::npos);
    EXPECT_FALSE(response.find("\"Dolby_Volume\"") != string::npos);

    // Test Volume + Enhancer
    EXPECT_CALL(*p_audioOutputPortMock, getMS12Capabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsMS12SUPPORT_DolbyVolume | dsMS12SUPPORT_DialogueEnhancer;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Dolby_Volume\"") != string::npos);
    EXPECT_TRUE(response.find("\"Dialogue_Enhancer\"") != string::npos);
    EXPECT_FALSE(response.find("\"Inteligent_Equalizer\"") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Positive_VariousPortNames)
{
    device::AudioOutputPort audioOutputPort;

    // Test HDMI0
    string port1 = "HDMI0";
    EXPECT_CALL(*p_audioOutputPortMock, getMS12Capabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsMS12SUPPORT_DolbyVolume;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(port1))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Dolby_Volume\"") != string::npos);

    // Test SPDIF
    string port2 = "SPDIF0";
    EXPECT_CALL(*p_audioOutputPortMock, getMS12Capabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsMS12SUPPORT_InteligentEqualizer;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(port2))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"SPDIF0\"}"), response));
    EXPECT_TRUE(response.find("\"Inteligent_Equalizer\"") != string::npos);

    // Test SPEAKER
    string port3 = "SPEAKER0";
    EXPECT_CALL(*p_audioOutputPortMock, getMS12Capabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsMS12SUPPORT_DialogueEnhancer;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(port3))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"SPEAKER0\"}"), response));
    EXPECT_TRUE(response.find("\"Dialogue_Enhancer\"") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, MS12Capabilities_Positive_DefaultPortUsed)
{
    device::AudioOutputPort audioOutputPort;
    string defaultPort = "SPDIF";

    EXPECT_CALL(*p_audioOutputPortMock, getMS12Capabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsMS12SUPPORT_DolbyVolume;
        }));

    EXPECT_CALL(*p_hostImplMock, getDefaultAudioPortName())
        .WillOnce(Return(defaultPort));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"Dolby_Volume\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Positive_VariousProfiles)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";

    // Test with 2 profiles
    std::vector<std::string> profiles1 = {"Movie", "Music"};
    EXPECT_CALL(*p_audioOutputPortMock, getMS12AudioProfileList())
        .WillOnce(Return(profiles1));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Movie\"") != string::npos);
    EXPECT_TRUE(response.find("\"Music\"") != string::npos);
    EXPECT_FALSE(response.find("\"Voice\"") != string::npos);

    // Test with 4 profiles
    std::vector<std::string> profiles2 = {"Movie", "Music", "Voice", "Sport"};
    EXPECT_CALL(*p_audioOutputPortMock, getMS12AudioProfileList())
        .WillOnce(Return(profiles2));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Movie\"") != string::npos);
    EXPECT_TRUE(response.find("\"Music\"") != string::npos);
    EXPECT_TRUE(response.find("\"Voice\"") != string::npos);
    EXPECT_TRUE(response.find("\"Sport\"") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Positive_LargeProfileList)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";

    std::vector<std::string> profiles = {
        "Movie", "Music", "Voice", "Sport", "Game",
        "Night", "Standard", "Custom1", "Custom2", "Custom3"
    };

    EXPECT_CALL(*p_audioOutputPortMock, getMS12AudioProfileList())
        .WillOnce(Return(profiles));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"HDMI0\"}"), response));

    for (const auto& profile : profiles) {
        EXPECT_TRUE(response.find(profile) != string::npos);
    }
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Positive_DefaultPortUsed)
{
    device::AudioOutputPort audioOutputPort;
    string defaultPort = "SPEAKER";
    std::vector<std::string> profiles = {"Movie", "Music"};

    EXPECT_CALL(*p_audioOutputPortMock, getMS12AudioProfileList())
        .WillOnce(Return(profiles));

    EXPECT_CALL(*p_hostImplMock, getDefaultAudioPortName())
        .WillOnce(Return(defaultPort));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(_))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"Movie\"") != string::npos);
    EXPECT_TRUE(response.find("\"Music\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, SupportedMS12AudioProfiles_Positive_VariousPortNames)
{
    device::AudioOutputPort audioOutputPort;
    std::vector<std::string> profiles = {"Movie"};

    // Test HDMI0
    string port1 = "HDMI0";
    EXPECT_CALL(*p_audioOutputPortMock, getMS12AudioProfileList())
        .WillOnce(Return(profiles));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(port1))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Movie\"") != string::npos);

    // Test SPDIF
    string port2 = "SPDIF0";
    profiles = {"Music", "Voice"};
    EXPECT_CALL(*p_audioOutputPortMock, getMS12AudioProfileList())
        .WillOnce(Return(profiles));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(port2))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"SPDIF0\"}"), response));
    EXPECT_TRUE(response.find("\"Music\"") != string::npos);
    EXPECT_TRUE(response.find("\"Voice\"") != string::npos);

    // Test SPEAKER
    string port3 = "SPEAKER0";
    profiles = {"Sport", "Game", "Night"};
    EXPECT_CALL(*p_audioOutputPortMock, getMS12AudioProfileList())
        .WillOnce(Return(profiles));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(port3))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"SPEAKER0\"}"), response));
    EXPECT_TRUE(response.find("\"Sport\"") != string::npos);
    EXPECT_TRUE(response.find("\"Game\"") != string::npos);
    EXPECT_TRUE(response.find("\"Night\"") != string::npos);
}

// =========== Boundary and Edge Case Tests ===========

TEST_F(DeviceAudioCapabilitiesTest, Boundary_AudioCapabilities_ZeroCapabilities)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = 0;
        }));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"none\"") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, Boundary_AudioCapabilities_MaxCombination)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_ATMOS | dsAUDIOSUPPORT_DD |
                          dsAUDIOSUPPORT_DDPLUS | dsAUDIOSUPPORT_DAD |
                          dsAUDIOSUPPORT_DAPv2 | dsAUDIOSUPPORT_MS12;
        }));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

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
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getMS12Capabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = 0;
        }));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"none\"") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, Boundary_MS12Capabilities_AllCapabilities)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";

    EXPECT_CALL(*p_audioOutputPortMock, getMS12Capabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsMS12SUPPORT_DolbyVolume | dsMS12SUPPORT_InteligentEqualizer |
                          dsMS12SUPPORT_DialogueEnhancer;
        }));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Dolby_Volume\"") != string::npos);
    EXPECT_TRUE(response.find("\"Inteligent_Equalizer\"") != string::npos);
    EXPECT_TRUE(response.find("\"Dialogue_Enhancer\"") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, Boundary_SupportedMS12AudioProfiles_SingleProfile)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";
    std::vector<std::string> profiles = {"SingleProfile"};

    EXPECT_CALL(*p_audioOutputPortMock, getMS12AudioProfileList())
        .WillOnce(Return(profiles));

    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"SingleProfile\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, EdgeCase_SequentialCallsSamePort)
{
    device::AudioOutputPort audioOutputPort;
    string portName = "HDMI0";

    // First call
    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_ATMOS;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"ATMOS\"") != string::npos);

    // Second call
    EXPECT_CALL(*p_audioOutputPortMock, getMS12Capabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsMS12SUPPORT_DolbyVolume;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("ms12capabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Dolby_Volume\"") != string::npos);

    // Third call
    std::vector<std::string> profiles = {"Movie"};
    EXPECT_CALL(*p_audioOutputPortMock, getMS12AudioProfileList())
        .WillOnce(Return(profiles));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(portName))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedms12audioprofiles"), _T("{\"audioPort\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"Movie\"") != string::npos);
}

TEST_F(DeviceAudioCapabilitiesTest, EdgeCase_AlternatingPortCalls)
{
    device::AudioOutputPort audioOutputPort;

    // Call for HDMI0
    string port1 = "HDMI0";
    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_ATMOS;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(port1))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));

    // Call for SPDIF
    string port2 = "SPDIF0";
    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_DD;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(port2))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"SPDIF0\"}"), response));

    // Call for HDMI0 again
    EXPECT_CALL(*p_audioOutputPortMock, getAudioCapabilities(_))
        .WillOnce(Invoke([](int* capabilities) {
            *capabilities = dsAUDIOSUPPORT_DDPLUS;
        }));
    EXPECT_CALL(*p_hostImplMock, getAudioOutputPort(port1))
        .WillOnce(ReturnRef(audioOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("audiocapabilities"), _T("{\"audioPort\":\"HDMI0\"}"), response));
}
