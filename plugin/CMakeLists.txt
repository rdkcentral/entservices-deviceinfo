# If not stated otherwise in this file or this component's LICENSE file the
# following copyright and licenses apply:
#
# Copyright 2020 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set(PLUGIN_NAME DeviceInfo)
set(MODULE_NAME ${NAMESPACE}${PLUGIN_NAME})
set(PLUGIN_IMPLEMENTATION ${MODULE_NAME}Implementation)

set(PLUGIN_DEVICEINFO_MODE "Off" CACHE STRING "Controls if the plugin should run in its own process, in process or remote")
set(PLUGIN_DEVICEINFO_STARTUPORDER "" CACHE STRING "Start-up order for DeviceInfo plugin")

find_package(${NAMESPACE}Plugins REQUIRED)
find_package(${NAMESPACE}Definitions REQUIRED)
find_package(CompileSettingsDebug CONFIG REQUIRED)
find_package(RFC)
find_package(DS)
find_package(IARMBus)

add_library(${MODULE_NAME} SHARED
        DeviceInfo.cpp
        Module.cpp)

set_target_properties(${MODULE_NAME} PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES)

include_directories(
        ../helpers)

target_link_libraries(${MODULE_NAME}
    PRIVATE
        CompileSettingsDebug::CompileSettingsDebug
        ${NAMESPACE}Plugins::${NAMESPACE}Plugins
        ${NAMESPACE}Definitions::${NAMESPACE}Definitions)

install(TARGETS ${MODULE_NAME}
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/${STORAGE_DIRECTORY}/plugins)

add_library(${PLUGIN_IMPLEMENTATION} SHARED
    DeviceInfoImplementation.cpp
    DeviceAudioCapabilities.cpp
    DeviceVideoCapabilities.cpp
    Module.cpp)

target_include_directories(${PLUGIN_IMPLEMENTATION} PRIVATE
        ${RFC_INCLUDE_DIRS}
        ${DS_INCLUDE_DIRS}
        ${IARMBUS_INCLUDE_DIRS}
        ../helpers)

set_target_properties(${PLUGIN_IMPLEMENTATION} PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES)

set_source_files_properties(
        DeviceAudioCapabilities.cpp
        DeviceVideoCapabilities.cpp
        PROPERTIES
        COMPILE_FLAGS "-fexceptions")

target_link_libraries(${PLUGIN_IMPLEMENTATION}
        PRIVATE
        CompileSettingsDebug::CompileSettingsDebug
        ${NAMESPACE}Plugins::${NAMESPACE}Plugins
        ${NAMESPACE}Definitions::${NAMESPACE}Definitions
        ${RFC_LIBRARIES}
        ${DS_LIBRARIES}
        ${IARMBUS_LIBRARIES})

install(TARGETS ${PLUGIN_IMPLEMENTATION}
        DESTINATION lib/${STORAGE_DIRECTORY}/plugins)

write_config(${PLUGIN_NAME})
