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
#include "DeviceVideoCapabilities.h"
#include "HostMock.h"
#include "IarmBusMock.h"
#include "ManagerMock.h"
#include "ServiceMock.h"
#include "VideoOutputPortConfigMock.h"
#include "VideoOutputPortMock.h"
#include "VideoOutputPortTypeMock.h"
#include "VideoResolutionMock.h"
#include "SystemInfo.h"
#include <fstream>
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

class DeviceVideoCapabilitiesTest : public ::testing::Test {
protected:
    Core::ProxyType<Plugin::DeviceInfo> plugin;
    Core::JSONRPC::Handler& handler;
    DECL_CORE_JSONRPC_CONX connection;
    string response;

    IarmBusImplMock* p_iarmBusImplMock = nullptr;
    ManagerImplMock* p_managerImplMock = nullptr;
    HostImplMock* p_hostImplMock = nullptr;
    VideoOutputPortMock* p_videoOutputPortMock = nullptr;
    VideoOutputPortConfigImplMock* p_videoOutputPortConfigImplMock = nullptr;
    VideoOutputPortTypeMock* p_videoOutputPortTypeMock = nullptr;
    VideoResolutionMock* p_videoResolutionMock = nullptr;
    NiceMock<ServiceMock> service;

    DeviceVideoCapabilitiesTest()
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

        p_videoOutputPortMock = new NiceMock<VideoOutputPortMock>;
        device::VideoOutputPort::setImpl(p_videoOutputPortMock);

        p_videoOutputPortConfigImplMock = new NiceMock<VideoOutputPortConfigImplMock>;
        device::VideoOutputPortConfig::setImpl(p_videoOutputPortConfigImplMock);

        p_videoOutputPortTypeMock = new NiceMock<VideoOutputPortTypeMock>;
        device::VideoOutputPortType::setImpl(p_videoOutputPortTypeMock);

        p_videoResolutionMock = new NiceMock<VideoResolutionMock>;
        device::VideoResolution::setImpl(p_videoResolutionMock);

        ON_CALL(service, ConfigLine())
            .WillByDefault(Return("{\"root\":{\"mode\":\"Off\"}}"));
        ON_CALL(service, WebPrefix())
            .WillByDefault(Return(webPrefix));

        EXPECT_EQ(string(""), plugin->Initialize(&service));
    }

    virtual ~DeviceVideoCapabilitiesTest()
    {
        plugin->Deinitialize(&service);

        device::VideoResolution::setImpl(nullptr);
        if (p_videoResolutionMock != nullptr) {
            delete p_videoResolutionMock;
            p_videoResolutionMock = nullptr;
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

TEST_F(DeviceVideoCapabilitiesTest, SupportedVideoDisplays_Success_SingleDisplay)
{
    device::VideoOutputPort videoPort;
    device::List<device::VideoOutputPort> videoPorts;
    string portName = "HDMI0";

    EXPECT_CALL(*p_videoOutputPortMock, getName())
        .WillOnce(ReturnRef(portName));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPorts())
        .WillOnce(Invoke([&]() {
            videoPorts.push_back(videoPort);
            return videoPorts;
        }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedvideodisplays"), _T(""), response));
    EXPECT_TRUE(response.find("\"supportedVideoDisplays\":[") != string::npos);
    EXPECT_TRUE(response.find("\"HDMI0\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedVideoDisplays_Success_MultipleDisplays)
{
    device::VideoOutputPort videoPort1, videoPort2;
    device::List<device::VideoOutputPort> videoPorts;
    string portName1 = "HDMI0";
    string portName2 = "HDMI1";

    EXPECT_CALL(*p_videoOutputPortMock, getName())
        .WillOnce(ReturnRef(portName1))
        .WillOnce(ReturnRef(portName2));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPorts())
        .WillOnce(Invoke([&]() {
            videoPorts.push_back(videoPort1);
            videoPorts.push_back(videoPort2);
            return videoPorts;
        }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedvideodisplays"), _T(""), response));
    EXPECT_TRUE(response.find("\"HDMI0\"") != string::npos);
    EXPECT_TRUE(response.find("\"HDMI1\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedVideoDisplays_Success_DuplicatePortsFiltered)
{
    device::VideoOutputPort videoPort1, videoPort2, videoPort3;
    device::List<device::VideoOutputPort> videoPorts;
    string portName1 = "HDMI0";
    string portName2 = "HDMI0";
    string portName3 = "HDMI1";

    EXPECT_CALL(*p_videoOutputPortMock, getName())
        .WillOnce(ReturnRef(portName1))
        .WillOnce(ReturnRef(portName2))
        .WillOnce(ReturnRef(portName3));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPorts())
        .WillOnce(Invoke([&]() {
            videoPorts.push_back(videoPort1);
            videoPorts.push_back(videoPort2);
            videoPorts.push_back(videoPort3);
            return videoPorts;
        }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedvideodisplays"), _T(""), response));
    EXPECT_TRUE(response.find("\"HDMI0\"") != string::npos);
    EXPECT_TRUE(response.find("\"HDMI1\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedVideoDisplays_Success_EmptyList)
{
    device::List<device::VideoOutputPort> videoPorts;

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPorts())
        .WillOnce(Return(videoPorts));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedvideodisplays"), _T(""), response));
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedVideoDisplays_Failure_DeviceException)
{
    EXPECT_CALL(*p_hostImplMock, getVideoOutputPorts())
        .WillOnce(Invoke([]() -> device::List<device::VideoOutputPort> {
            throw device::Exception("Test device exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedvideodisplays"), _T(""), response));
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedVideoDisplays_Failure_StdException)
{
    EXPECT_CALL(*p_hostImplMock, getVideoOutputPorts())
        .WillOnce(Invoke([]() -> device::List<device::VideoOutputPort> {
            throw std::runtime_error("Test std exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedvideodisplays"), _T(""), response));
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedVideoDisplays_Failure_UnknownException)
{
    EXPECT_CALL(*p_hostImplMock, getVideoOutputPorts())
        .WillOnce(Invoke([]() -> device::List<device::VideoOutputPort> {
            throw 42;
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedvideodisplays"), _T(""), response));
}

TEST_F(DeviceVideoCapabilitiesTest, HostEDID_Success_ValidEDID)
{
    std::vector<uint8_t> edidData = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

    EXPECT_CALL(*p_hostImplMock, getHostEDID(_))
        .WillOnce(Invoke([&edidData](std::vector<uint8_t>& edid) {
            edid = edidData;
        }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("hostedid"), _T(""), response));
    EXPECT_TRUE(response.find("\"EDID\":") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, HostEDID_Success_EmptyEDID)
{
    std::vector<uint8_t> edidData;

    EXPECT_CALL(*p_hostImplMock, getHostEDID(_))
        .WillOnce(Invoke([&edidData](std::vector<uint8_t>& edid) {
            edid = edidData;
        }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("hostedid"), _T(""), response));
    EXPECT_TRUE(response.find("\"EDID\":") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, HostEDID_Failure_DeviceException)
{
    EXPECT_CALL(*p_hostImplMock, getHostEDID(_))
        .WillOnce(Invoke([](std::vector<uint8_t>&) {
            throw device::Exception("Test device exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("hostedid"), _T(""), response));
}

TEST_F(DeviceVideoCapabilitiesTest, HostEDID_Failure_StdException)
{
    EXPECT_CALL(*p_hostImplMock, getHostEDID(_))
        .WillOnce(Invoke([](std::vector<uint8_t>&) {
            throw std::runtime_error("Test std exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("hostedid"), _T(""), response));
}

TEST_F(DeviceVideoCapabilitiesTest, HostEDID_Failure_UnknownException)
{
    EXPECT_CALL(*p_hostImplMock, getHostEDID(_))
        .WillOnce(Invoke([](std::vector<uint8_t>&) {
            throw "Unknown error";
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("hostedid"), _T(""), response));
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Success_EmptyPort)
{
    device::VideoOutputPort videoPort;
    device::VideoResolution resolution;
    string defaultPort = "HDMI0";
    string resolutionName = "1080p";

    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(ReturnRef(resolutionName));

    EXPECT_CALL(*p_videoOutputPortMock, getDefaultResolution())
        .WillOnce(ReturnRef(resolution));

    EXPECT_CALL(*p_hostImplMock, getDefaultVideoPortName())
        .WillOnce(Return(defaultPort));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(_))
        .WillOnce(ReturnRef(videoPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"defaultResolution\":\"1080p\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Success_SpecificPort)
{
    device::VideoOutputPort videoPort;
    device::VideoResolution resolution;
    string portName = "HDMI1";
    string resolutionName = "720p";

    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(ReturnRef(resolutionName));

    EXPECT_CALL(*p_videoOutputPortMock, getDefaultResolution())
        .WillOnce(ReturnRef(resolution));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(portName))
        .WillOnce(ReturnRef(videoPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI1\"}"), response));
    EXPECT_TRUE(response.find("\"defaultResolution\":\"720p\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Success_4KResolution)
{
    device::VideoOutputPort videoPort;
    device::VideoResolution resolution;
    string portName = "HDMI0";
    string resolutionName = "2160p";

    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(ReturnRef(resolutionName));

    EXPECT_CALL(*p_videoOutputPortMock, getDefaultResolution())
        .WillOnce(ReturnRef(resolution));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(portName))
        .WillOnce(ReturnRef(videoPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"defaultResolution\":\"2160p\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Failure_DeviceException)
{
    string portName = "HDMI0";
    EXPECT_CALL(*p_hostImplMock, getDefaultVideoPortName())
        .WillOnce(Return(portName));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::VideoOutputPort& {
            throw device::Exception("Test device exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"\"}"), response));
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Failure_StdException)
{
    string portName = "HDMI0";
    EXPECT_CALL(*p_hostImplMock, getDefaultVideoPortName())
        .WillOnce(Return(portName));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::VideoOutputPort& {
            throw std::runtime_error("Test std exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"\"}"), response));
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Failure_UnknownException)
{
    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::VideoOutputPort& {
            throw 42;
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Success_EmptyPort_MultipleResolutions)
{
    device::VideoOutputPort videoPort;
    device::VideoOutputPortType portType;
    device::VideoResolution res1, res2, res3;
    device::List<device::VideoResolution> resolutions;
    string defaultPort = "HDMI0";
    string resName1 = "480p";
    string resName2 = "720p";
    string resName3 = "1080p";

    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(ReturnRef(resName1))
        .WillOnce(ReturnRef(resName2))
        .WillOnce(ReturnRef(resName3));

    EXPECT_CALL(*p_videoOutputPortTypeMock, getSupportedResolutions())
        .WillOnce(Invoke([&]() {
            resolutions.push_back(res1);
            resolutions.push_back(res2);
            resolutions.push_back(res3);
            return resolutions;
        }));

    EXPECT_CALL(*p_videoOutputPortTypeMock, getId())
        .WillOnce(Return(0));

    EXPECT_CALL(*p_videoOutputPortMock, getType())
        .WillOnce(ReturnRef(portType));

    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPortType(_))
        .WillOnce(ReturnRef(portType));

    EXPECT_CALL(*p_hostImplMock, getDefaultVideoPortName())
        .WillOnce(Return(defaultPort));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(_))
        .WillOnce(ReturnRef(videoPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"supportedResolutions\":[") != string::npos);
    EXPECT_TRUE(response.find("\"480p\"") != string::npos);
    EXPECT_TRUE(response.find("\"720p\"") != string::npos);
    EXPECT_TRUE(response.find("\"1080p\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Success_SpecificPort_SingleResolution)
{
    device::VideoOutputPort videoPort;
    device::VideoOutputPortType portType;
    device::VideoResolution res1;
    device::List<device::VideoResolution> resolutions;
    string portName = "HDMI1";
    string resName1 = "1080p";

    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(ReturnRef(resName1));

    EXPECT_CALL(*p_videoOutputPortTypeMock, getSupportedResolutions())
        .WillOnce(Invoke([&]() {
            resolutions.push_back(res1);
            return resolutions;
        }));

    EXPECT_CALL(*p_videoOutputPortTypeMock, getId())
        .WillOnce(Return(0));

    EXPECT_CALL(*p_videoOutputPortMock, getType())
        .WillOnce(ReturnRef(portType));

    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPortType(_))
        .WillOnce(ReturnRef(portType));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(portName))
        .WillOnce(ReturnRef(videoPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"HDMI1\"}"), response));
    EXPECT_TRUE(response.find("\"1080p\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Success_EmptyList)
{
    device::VideoOutputPort videoPort;
    device::VideoOutputPortType portType;
    device::List<device::VideoResolution> resolutions;
    string defaultPort = "HDMI0";

    EXPECT_CALL(*p_hostImplMock, getDefaultVideoPortName())
        .WillOnce(Return(defaultPort));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(_))
        .WillOnce(ReturnRef(videoPort));

    EXPECT_CALL(*p_videoOutputPortMock, getType())
        .WillOnce(ReturnRef(portType));

    EXPECT_CALL(*p_videoOutputPortTypeMock, getId())
        .WillOnce(Return(0));

    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPortType(_))
        .WillOnce(ReturnRef(portType));

    EXPECT_CALL(*p_videoOutputPortTypeMock, getSupportedResolutions())
        .WillOnce(Return(resolutions));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Failure_DeviceException)
{
    string portName = "HDMI0";
    EXPECT_CALL(*p_hostImplMock, getDefaultVideoPortName())
        .WillOnce(Return(portName));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::VideoOutputPort& {
            throw device::Exception("Test device exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"\"}"), response));
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Failure_StdException)
{
    string portName = "HDMI0";
    EXPECT_CALL(*p_hostImplMock, getDefaultVideoPortName())
        .WillOnce(Return(portName));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::VideoOutputPort& {
            throw std::runtime_error("Test std exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"\"}"), response));
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Failure_UnknownException)
{
    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::VideoOutputPort& {
            throw 99;
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Success_EmptyPort_HDCP22)
{
    device::VideoOutputPort videoPort;
    string defaultPort = "HDMI0";

    EXPECT_CALL(*p_videoOutputPortMock, getHDCPProtocol())
        .WillOnce(Return(dsHDCP_VERSION_2X));

    EXPECT_CALL(*p_hostImplMock, getDefaultVideoPortName())
        .WillOnce(Return(defaultPort));

    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPort(_))
        .WillOnce(ReturnRef(videoPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"supportedHDCPVersion\":\"2.2\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Success_SpecificPort_HDCP14)
{
    device::VideoOutputPort videoPort;
    string portName = "HDMI1";

    EXPECT_CALL(*p_videoOutputPortMock, getHDCPProtocol())
        .WillOnce(Return(dsHDCP_VERSION_1X));

    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPort(portName))
        .WillOnce(ReturnRef(videoPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI1\"}"), response));
    EXPECT_TRUE(response.find("\"supportedHDCPVersion\":\"1.4\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Success_EmptyPort_HDCP14)
{
    device::VideoOutputPort videoPort;
    string defaultPort = "HDMI0";

    EXPECT_CALL(*p_videoOutputPortMock, getHDCPProtocol())
        .WillOnce(Return(dsHDCP_VERSION_1X));

    EXPECT_CALL(*p_hostImplMock, getDefaultVideoPortName())
        .WillOnce(Return(defaultPort));

    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPort(_))
        .WillOnce(ReturnRef(videoPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"supportedHDCPVersion\":\"1.4\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Success_SpecificPort_HDCP22)
{
    device::VideoOutputPort videoPort;
    string portName = "HDMI0";

    EXPECT_CALL(*p_videoOutputPortMock, getHDCPProtocol())
        .WillOnce(Return(dsHDCP_VERSION_2X));

    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPort(portName))
        .WillOnce(ReturnRef(videoPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"supportedHDCPVersion\":\"2.2\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Failure_InvalidProtocol)
{
    device::VideoOutputPort videoPort;
    string portName = "HDMI0";

    EXPECT_CALL(*p_videoOutputPortMock, getHDCPProtocol())
        .WillOnce(Return(999));

    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPort(portName))
        .WillOnce(ReturnRef(videoPort));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Failure_DeviceException)
{
    string portName = "HDMI0";
    EXPECT_CALL(*p_hostImplMock, getDefaultVideoPortName())
        .WillOnce(Return(portName));

    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::VideoOutputPort& {
            throw device::Exception("Test device exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"\"}"), response));
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Failure_StdException)
{
    string portName = "HDMI0";
    EXPECT_CALL(*p_hostImplMock, getDefaultVideoPortName())
        .WillOnce(Return(portName));

    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::VideoOutputPort& {
            throw std::runtime_error("Test std exception");
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"\"}"), response));
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Failure_UnknownException)
{
    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPort(_))
        .WillOnce(Invoke([](const std::string&) -> device::VideoOutputPort& {
            throw "Unknown error";
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
}

// =========== Additional Negative Tests ===========

TEST_F(DeviceVideoCapabilitiesTest, SupportedVideoDisplays_Negative_GetNameThrowsException)
{
    device::VideoOutputPort videoPort;
    device::List<device::VideoOutputPort> videoPorts;

    EXPECT_CALL(*p_videoOutputPortMock, getName())
        .WillOnce(Invoke([]() -> const string& {
            throw device::Exception("getName exception");
            static string dummy = "HDMI0";
            return dummy;
        }));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPorts())
        .WillOnce(Invoke([&]() {
            videoPorts.push_back(videoPort);
            return videoPorts;
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedvideodisplays"), _T(""), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, HostEDID_Negative_EDIDSizeExceedsLimit)
{
    std::vector<uint8_t> largeEdidData(std::numeric_limits<uint16_t>::max() + 1, 0xFF);

    EXPECT_CALL(*p_hostImplMock, getHostEDID(_))
        .WillOnce(Invoke([&largeEdidData](std::vector<uint8_t>& edid) {
            edid = largeEdidData;
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("hostedid"), _T(""), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, HostEDID_Negative_GetHostEDIDThrowsInvoke)
{
    EXPECT_CALL(*p_hostImplMock, getHostEDID(_))
        .WillOnce(Invoke([](std::vector<uint8_t>& edid) {
            throw device::Exception("getHostEDID exception");
            edid = {0x00, 0xFF};
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("hostedid"), _T(""), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Negative_InvalidVideoDisplay)
{
    string invalidPort = "INVALID_PORT";

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(invalidPort))
        .WillOnce(Invoke([](const std::string&) -> device::VideoOutputPort& {
            throw device::Exception("Invalid port name");
            static device::VideoOutputPort dummy;
            return dummy;
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"INVALID_PORT\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Negative_GetDefaultResolutionThrows)
{
    device::VideoOutputPort videoPort;
    string portName = "HDMI0";

    EXPECT_CALL(*p_videoOutputPortMock, getDefaultResolution())
        .WillOnce(Invoke([]() -> const device::VideoResolution& {
            throw device::Exception("getDefaultResolution exception");
            static device::VideoResolution dummy;
            return dummy;
        }));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(portName))
        .WillOnce(ReturnRef(videoPort));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Negative_GetNameThrowsException)
{
    device::VideoOutputPort videoPort;
    device::VideoResolution resolution;
    string portName = "HDMI0";

    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(Invoke([]() -> const string& {
            throw std::runtime_error("getName exception");
            static string dummy = "1080p";
            return dummy;
        }));

    EXPECT_CALL(*p_videoOutputPortMock, getDefaultResolution())
        .WillOnce(ReturnRef(resolution));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(portName))
        .WillOnce(ReturnRef(videoPort));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Negative_GetDefaultVideoPortNameThrows)
{
    EXPECT_CALL(*p_hostImplMock, getDefaultVideoPortName())
        .WillOnce(Invoke([]() -> std::string {
            throw device::Exception("getDefaultVideoPortName exception");
            return "HDMI0";
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Negative_InvalidVideoDisplay)
{
    string invalidPort = "INVALID_PORT";

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(invalidPort))
        .WillOnce(Invoke([](const std::string&) -> device::VideoOutputPort& {
            throw device::Exception("Invalid port name");
            static device::VideoOutputPort dummy;
            return dummy;
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"INVALID_PORT\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Negative_GetTypeThrowsException)
{
    device::VideoOutputPort videoPort;
    string portName = "HDMI0";

    EXPECT_CALL(*p_videoOutputPortMock, getType())
        .WillOnce(Invoke([]() -> const device::VideoOutputPortType& {
            throw device::Exception("getType exception");
            static device::VideoOutputPortType dummy;
            return dummy;
        }));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(portName))
        .WillOnce(ReturnRef(videoPort));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Negative_GetPortTypeThrowsException)
{
    device::VideoOutputPort videoPort;
    device::VideoOutputPortType portType;
    string portName = "HDMI0";

    EXPECT_CALL(*p_videoOutputPortTypeMock, getId())
        .WillOnce(Return(0));

    EXPECT_CALL(*p_videoOutputPortMock, getType())
        .WillOnce(ReturnRef(portType));

    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPortType(_))
        .WillOnce(Invoke([](int) -> device::VideoOutputPortType& {
            throw device::Exception("getPortType exception");
            static device::VideoOutputPortType dummy;
            return dummy;
        }));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(portName))
        .WillOnce(ReturnRef(videoPort));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Negative_GetSupportedResolutionsThrows)
{
    device::VideoOutputPort videoPort;
    device::VideoOutputPortType portType;
    string portName = "HDMI0";

    EXPECT_CALL(*p_videoOutputPortTypeMock, getSupportedResolutions())
        .WillOnce(Invoke([]() -> device::List<device::VideoResolution> {
            throw std::runtime_error("getSupportedResolutions exception");
            return device::List<device::VideoResolution>();
        }));

    EXPECT_CALL(*p_videoOutputPortTypeMock, getId())
        .WillOnce(Return(0));

    EXPECT_CALL(*p_videoOutputPortMock, getType())
        .WillOnce(ReturnRef(portType));

    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPortType(_))
        .WillOnce(ReturnRef(portType));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(portName))
        .WillOnce(ReturnRef(videoPort));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Negative_ResolutionGetNameThrows)
{
    device::VideoOutputPort videoPort;
    device::VideoOutputPortType portType;
    device::VideoResolution res1;
    device::List<device::VideoResolution> resolutions;
    string portName = "HDMI0";

    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(Invoke([]() -> const string& {
            throw device::Exception("getName exception");
            static string dummy = "1080p";
            return dummy;
        }));

    EXPECT_CALL(*p_videoOutputPortTypeMock, getSupportedResolutions())
        .WillOnce(Invoke([&]() {
            resolutions.push_back(res1);
            return resolutions;
        }));

    EXPECT_CALL(*p_videoOutputPortTypeMock, getId())
        .WillOnce(Return(0));

    EXPECT_CALL(*p_videoOutputPortMock, getType())
        .WillOnce(ReturnRef(portType));

    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPortType(_))
        .WillOnce(ReturnRef(portType));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(portName))
        .WillOnce(ReturnRef(videoPort));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Negative_GetDefaultVideoPortNameThrows)
{
    EXPECT_CALL(*p_hostImplMock, getDefaultVideoPortName())
        .WillOnce(Invoke([]() -> std::string {
            throw std::runtime_error("getDefaultVideoPortName exception");
            return "HDMI0";
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Negative_InvalidVideoDisplay)
{
    string invalidPort = "INVALID_PORT";

    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPort(invalidPort))
        .WillOnce(Invoke([](const std::string&) -> device::VideoOutputPort& {
            throw device::Exception("Invalid port name");
            static device::VideoOutputPort dummy;
            return dummy;
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"INVALID_PORT\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Negative_GetHDCPProtocolThrows)
{
    device::VideoOutputPort videoPort;
    string portName = "HDMI0";

    EXPECT_CALL(*p_videoOutputPortMock, getHDCPProtocol())
        .WillOnce(Invoke([]() -> dsHdcpProtocolVersion_t {
            throw device::Exception("getHDCPProtocol exception");
            return dsHDCP_VERSION_2X;
        }));

    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPort(portName))
        .WillOnce(ReturnRef(videoPort));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Negative_GetDefaultVideoPortNameThrows)
{
    EXPECT_CALL(*p_hostImplMock, getDefaultVideoPortName())
        .WillOnce(Invoke([]() -> std::string {
            throw device::Exception("getDefaultVideoPortName exception");
            return "HDMI0";
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"\"}"), response));
    EXPECT_TRUE(response.empty());
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Negative_GetPortFromConfigThrows)
{
    string portName = "HDMI0";

    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPort(portName))
        .WillOnce(Invoke([](const std::string&) -> device::VideoOutputPort& {
            throw std::runtime_error("getPort exception");
            static device::VideoOutputPort dummy;
            return dummy;
        }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.empty());
}

// =========== Additional Comprehensive Positive Tests ===========

TEST_F(DeviceVideoCapabilitiesTest, SupportedVideoDisplays_Positive_SingleDisplay)
{
    device::List<device::VideoOutputPort> vPorts;
    device::VideoOutputPort videoOutputPort;

    static const string portName = "HDMI0";
    EXPECT_CALL(*p_videoOutputPortMock, getName())
        .WillOnce(ReturnRef(portName));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPorts())
        .WillOnce(Invoke([&]() {
            vPorts.push_back(videoOutputPort);
            return vPorts;
        }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedvideodisplays"), _T("{}"), response));
    EXPECT_TRUE(response.find("\"HDMI0\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedVideoDisplays_Positive_VariousPortTypes)
{
    device::List<device::VideoOutputPort> vPorts;
    device::VideoOutputPort port1, port2, port3, port4, port5;

    static const string portName1 = "HDMI0";
    static const string portName2 = "HDMI1";
    static const string portName3 = "Component";
    static const string portName4 = "Composite";
    static const string portName5 = "SVVideo";

    EXPECT_CALL(*p_videoOutputPortMock, getName())
        .WillOnce(ReturnRef(portName1))
        .WillOnce(ReturnRef(portName2))
        .WillOnce(ReturnRef(portName3))
        .WillOnce(ReturnRef(portName4))
        .WillOnce(ReturnRef(portName5));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPorts())
        .WillOnce(Invoke([&]() {
            vPorts.push_back(port1);
            vPorts.push_back(port2);
            vPorts.push_back(port3);
            vPorts.push_back(port4);
            vPorts.push_back(port5);
            return vPorts;
        }));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedvideodisplays"), _T("{}"), response));
    EXPECT_TRUE(response.find("\"HDMI0\"") != string::npos);
    EXPECT_TRUE(response.find("\"HDMI1\"") != string::npos);
    EXPECT_TRUE(response.find("\"Component\"") != string::npos);
    EXPECT_TRUE(response.find("\"Composite\"") != string::npos);
    EXPECT_TRUE(response.find("\"SVVideo\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Positive_VariousResolutions)
{
    device::VideoOutputPort videoOutputPort;
    device::VideoResolution videoResolution;
    string portName = "HDMI0";
    static const string res1080p = "1080p";
    static const string res720p = "720p";
    static const string res2160p = "2160p";
    static const string res480i = "480i";

    // Test 1080p
    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(ReturnRef(res1080p));
    EXPECT_CALL(*p_videoOutputPortMock, getDefaultResolution())
        .WillOnce(ReturnRef(videoResolution));
    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(portName))
        .WillOnce(ReturnRef(videoOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"1080p\"") != string::npos);

    // Test 720p
    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(ReturnRef(res720p));
    EXPECT_CALL(*p_videoOutputPortMock, getDefaultResolution())
        .WillOnce(ReturnRef(videoResolution));
    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(portName))
        .WillOnce(ReturnRef(videoOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"720p\"") != string::npos);

    // Test 4K
    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(ReturnRef(res2160p));
    EXPECT_CALL(*p_videoOutputPortMock, getDefaultResolution())
        .WillOnce(ReturnRef(videoResolution));
    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(portName))
        .WillOnce(ReturnRef(videoOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"2160p\"") != string::npos);

    // Test 480i
    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(ReturnRef(res480i));
    EXPECT_CALL(*p_videoOutputPortMock, getDefaultResolution())
        .WillOnce(ReturnRef(videoResolution));
    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(portName))
        .WillOnce(ReturnRef(videoOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"480i\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Positive_VariousPortNames)
{
    device::VideoOutputPort videoOutputPort;
    device::VideoResolution videoResolution;
    static const string res1080p = "1080p";
    static const string res720p = "720p";
    static const string res480i = "480i";

    // Test HDMI0
    string port1 = "HDMI0";
    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(ReturnRef(res1080p));
    EXPECT_CALL(*p_videoOutputPortMock, getDefaultResolution())
        .WillOnce(ReturnRef(videoResolution));
    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(port1))
        .WillOnce(ReturnRef(videoOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"1080p\"") != string::npos);

    // Test HDMI1
    string port2 = "HDMI1";
    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(ReturnRef(res720p));
    EXPECT_CALL(*p_videoOutputPortMock, getDefaultResolution())
        .WillOnce(ReturnRef(videoResolution));
    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(port2))
        .WillOnce(ReturnRef(videoOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI1\"}"), response));
    EXPECT_TRUE(response.find("\"720p\"") != string::npos);

    // Test Component
    string port3 = "Component";
    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(ReturnRef(res480i));
    EXPECT_CALL(*p_videoOutputPortMock, getDefaultResolution())
        .WillOnce(ReturnRef(videoResolution));
    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(port3))
        .WillOnce(ReturnRef(videoOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"Component\"}"), response));
    EXPECT_TRUE(response.find("\"480i\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, DefaultResolution_Positive_DefaultPortUsed)
{
    device::VideoOutputPort videoOutputPort;
    device::VideoResolution videoResolution;
    string defaultPort = "HDMI1";
    static const string res1080p = "1080p";

    EXPECT_CALL(*p_hostImplMock, getDefaultVideoPortName())
        .WillOnce(Return(defaultPort));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(_))
        .WillOnce(ReturnRef(videoOutputPort));

    EXPECT_CALL(*p_videoOutputPortMock, getDefaultResolution())
        .WillOnce(ReturnRef(videoResolution));

    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(ReturnRef(res1080p));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"1080p\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Positive_SingleResolution)
{
    device::VideoOutputPort videoOutputPort;
    device::VideoOutputPortType videoOutputPortType;
    device::VideoResolution resolution1;
    device::List<device::VideoResolution> resolutions;
    string portName = "HDMI0";
    static const string res1080p = "1080p";

    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(ReturnRef(res1080p));

    EXPECT_CALL(*p_videoOutputPortTypeMock, getSupportedResolutions())
        .WillOnce(Invoke([&]() {
            resolutions.push_back(resolution1);
            return resolutions;
        }));

    EXPECT_CALL(*p_videoOutputPortTypeMock, getId())
        .WillOnce(Return(0));

    EXPECT_CALL(*p_videoOutputPortMock, getType())
        .WillOnce(ReturnRef(videoOutputPortType));

    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPortType(_))
        .WillOnce(ReturnRef(videoOutputPortType));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(portName))
        .WillOnce(ReturnRef(videoOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"1080p\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Positive_LargeResolutionList)
{
    device::VideoOutputPort videoOutputPort;
    device::VideoOutputPortType videoOutputPortType;
    device::VideoResolution res1, res2, res3, res4, res5, res6, res7;
    device::List<device::VideoResolution> resolutions;
    string portName = "HDMI0";
    static const string res2160p = "2160p";
    static const string res1080p = "1080p";
    static const string res1080i = "1080i";
    static const string res720p = "720p";
    static const string res576p = "576p";
    static const string res480p = "480p";
    static const string res480i = "480i";

    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(ReturnRef(res2160p))
        .WillOnce(ReturnRef(res1080p))
        .WillOnce(ReturnRef(res1080i))
        .WillOnce(ReturnRef(res720p))
        .WillOnce(ReturnRef(res576p))
        .WillOnce(ReturnRef(res480p))
        .WillOnce(ReturnRef(res480i));

    EXPECT_CALL(*p_videoOutputPortTypeMock, getSupportedResolutions())
        .WillOnce(Invoke([&]() {
            resolutions.push_back(res1);
            resolutions.push_back(res2);
            resolutions.push_back(res3);
            resolutions.push_back(res4);
            resolutions.push_back(res5);
            resolutions.push_back(res6);
            resolutions.push_back(res7);
            return resolutions;
        }));

    EXPECT_CALL(*p_videoOutputPortTypeMock, getId())
        .WillOnce(Return(0));

    EXPECT_CALL(*p_videoOutputPortMock, getType())
        .WillOnce(ReturnRef(videoOutputPortType));

    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPortType(_))
        .WillOnce(ReturnRef(videoOutputPortType));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(portName))
        .WillOnce(ReturnRef(videoOutputPort));

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
    device::VideoOutputPort videoOutputPort;
    device::VideoOutputPortType videoOutputPortType;
    device::VideoResolution resolution1, resolution2;
    device::List<device::VideoResolution> resolutions;
    static const string res1080p = "1080p";
    static const string res720p = "720p";
    static const string res480i = "480i";

    // Test HDMI0
    string port1 = "HDMI0";
    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(ReturnRef(res1080p))
        .WillOnce(ReturnRef(res720p));
    EXPECT_CALL(*p_videoOutputPortTypeMock, getSupportedResolutions())
        .WillOnce(Invoke([&]() {
            resolutions.push_back(resolution1);
            resolutions.push_back(resolution2);
            return resolutions;
        }));
    EXPECT_CALL(*p_videoOutputPortTypeMock, getId())
        .WillOnce(Return(0));
    EXPECT_CALL(*p_videoOutputPortMock, getType())
        .WillOnce(ReturnRef(videoOutputPortType));
    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPortType(_))
        .WillOnce(ReturnRef(videoOutputPortType));
    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(port1))
        .WillOnce(ReturnRef(videoOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"1080p\"") != string::npos);

    // Test Component
    resolutions.clear();
    string port2 = "Component";
    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(ReturnRef(res480i));
    EXPECT_CALL(*p_videoOutputPortTypeMock, getSupportedResolutions())
        .WillOnce(Invoke([&]() {
            resolutions.push_back(resolution1);
            return resolutions;
        }));
    EXPECT_CALL(*p_videoOutputPortTypeMock, getId())
        .WillOnce(Return(0));
    EXPECT_CALL(*p_videoOutputPortMock, getType())
        .WillOnce(ReturnRef(videoOutputPortType));
    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPortType(_))
        .WillOnce(ReturnRef(videoOutputPortType));
    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(port2))
        .WillOnce(ReturnRef(videoOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"Component\"}"), response));
    EXPECT_TRUE(response.find("\"480i\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedResolutions_Positive_DefaultPortUsed)
{
    device::VideoOutputPort videoOutputPort;
    device::VideoOutputPortType videoOutputPortType;
    device::VideoResolution resolution1;
    device::List<device::VideoResolution> resolutions;
    string defaultPort = "HDMI1";
    static const string res1080p = "1080p";

    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(ReturnRef(res1080p));

    EXPECT_CALL(*p_videoOutputPortTypeMock, getSupportedResolutions())
        .WillOnce(Invoke([&]() {
            resolutions.push_back(resolution1);
            return resolutions;
        }));

    EXPECT_CALL(*p_videoOutputPortTypeMock, getId())
        .WillOnce(Return(0));

    EXPECT_CALL(*p_videoOutputPortMock, getType())
        .WillOnce(ReturnRef(videoOutputPortType));

    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPortType(_))
        .WillOnce(ReturnRef(videoOutputPortType));

    EXPECT_CALL(*p_hostImplMock, getDefaultVideoPortName())
        .WillOnce(Return(defaultPort));

    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(_))
        .WillOnce(ReturnRef(videoOutputPort));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedresolutions"), _T("{\"videoDisplay\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"1080p\"") != string::npos);
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Positive_VariousHdcpVersions)
{
    device::VideoOutputPort videoOutputPort;
    string portName = "HDMI0";

    // Test HDCP 1.4
    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPort(portName))
        .WillOnce(ReturnRef(videoOutputPort));
    EXPECT_CALL(*p_videoOutputPortMock, getHDCPProtocol())
        .WillOnce(Return(dsHDCP_VERSION_1X));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"supportedHDCPVersion\":\"1.4\"") != string::npos);

    // Test HDCP 2.2
    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPort(portName))
        .WillOnce(ReturnRef(videoOutputPort));
    EXPECT_CALL(*p_videoOutputPortMock, getHDCPProtocol())
        .WillOnce(Return(dsHDCP_VERSION_2X));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"supportedHDCPVersion\":\"2.2\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Positive_VariousPortNames)
{
    device::VideoOutputPort videoOutputPort;

    // Test HDMI0
    string port1 = "HDMI0";
    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPort(port1))
        .WillOnce(ReturnRef(videoOutputPort));
    EXPECT_CALL(*p_videoOutputPortMock, getHDCPProtocol())
        .WillOnce(Return(dsHDCP_VERSION_2X));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"supportedHDCPVersion\":\"2.2\"") != string::npos);

    // Test HDMI1
    string port2 = "HDMI1";
    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPort(port2))
        .WillOnce(ReturnRef(videoOutputPort));
    EXPECT_CALL(*p_videoOutputPortMock, getHDCPProtocol())
        .WillOnce(Return(dsHDCP_VERSION_1X));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI1\"}"), response));
    EXPECT_TRUE(response.find("\"supportedHDCPVersion\":\"1.4\"") != string::npos);

    // Test HDMI2
    string port3 = "HDMI2";
    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPort(port3))
        .WillOnce(ReturnRef(videoOutputPort));
    EXPECT_CALL(*p_videoOutputPortMock, getHDCPProtocol())
        .WillOnce(Return(dsHDCP_VERSION_2X));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI2\"}"), response));
    EXPECT_TRUE(response.find("\"supportedHDCPVersion\":\"2.2\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, SupportedHdcp_Positive_DefaultPortUsed)
{
    device::VideoOutputPort videoOutputPort;
    string defaultPort = "HDMI0";

    EXPECT_CALL(*p_hostImplMock, getDefaultVideoPortName())
        .WillOnce(Return(defaultPort));

    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPort(_))
        .WillOnce(ReturnRef(videoOutputPort));

    EXPECT_CALL(*p_videoOutputPortMock, getHDCPProtocol())
        .WillOnce(Return(dsHDCP_VERSION_2X));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"\"}"), response));
    EXPECT_TRUE(response.find("\"supportedHDCPVersion\":\"2.2\"") != string::npos);
}

// =========== Boundary and Edge Case Tests ===========
TEST_F(DeviceVideoCapabilitiesTest, EdgeCase_SequentialCallsSamePort)
{
    device::VideoOutputPort videoOutputPort;
    device::VideoResolution videoResolution;
    string portName = "HDMI0";
    static const string res1080p = "1080p";

    // First call - defaultresolution
    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(ReturnRef(res1080p));
    EXPECT_CALL(*p_videoOutputPortMock, getDefaultResolution())
        .WillOnce(ReturnRef(videoResolution));
    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(portName))
        .WillOnce(ReturnRef(videoOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"1080p\"") != string::npos);

    // Second call - supportedhdcp
    EXPECT_CALL(*p_videoOutputPortConfigImplMock, getPort(portName))
        .WillOnce(ReturnRef(videoOutputPort));
    EXPECT_CALL(*p_videoOutputPortMock, getHDCPProtocol())
        .WillOnce(Return(dsHDCP_VERSION_2X));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("supportedhdcp"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
    EXPECT_TRUE(response.find("\"supportedHDCPVersion\":\"2.2\"") != string::npos);
}

TEST_F(DeviceVideoCapabilitiesTest, EdgeCase_AlternatingPortCalls)
{
    device::VideoOutputPort videoOutputPort;
    device::VideoResolution videoResolution;
    static const string res1080p = "1080p";
    static const string res720p = "720p";
    static const string res2160p = "2160p";

    // Call for HDMI0
    string port1 = "HDMI0";
    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(ReturnRef(res1080p));
    EXPECT_CALL(*p_videoOutputPortMock, getDefaultResolution())
        .WillOnce(ReturnRef(videoResolution));
    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(port1))
        .WillOnce(ReturnRef(videoOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));

    // Call for HDMI1
    string port2 = "HDMI1";
    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(ReturnRef(res720p));
    EXPECT_CALL(*p_videoOutputPortMock, getDefaultResolution())
        .WillOnce(ReturnRef(videoResolution));
    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(port2))
        .WillOnce(ReturnRef(videoOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI1\"}"), response));

    // Call for HDMI0 again
    EXPECT_CALL(*p_videoResolutionMock, getName())
        .WillOnce(ReturnRef(res2160p));
    EXPECT_CALL(*p_videoOutputPortMock, getDefaultResolution())
        .WillOnce(ReturnRef(videoResolution));
    EXPECT_CALL(*p_hostImplMock, getVideoOutputPort(port1))
        .WillOnce(ReturnRef(videoOutputPort));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("defaultresolution"), _T("{\"videoDisplay\":\"HDMI0\"}"), response));
}
