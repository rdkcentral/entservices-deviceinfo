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

# -----------------------------------------------------------------
# USE_DEVICESETTING_PLUGIN: use DeviceSettings Thunder plugin via
# COM-RPC instead of libds/IARM for DeviceInfo capability queries.
# Enable at configure time: cmake -DUSE_DEVICESETTING_PLUGIN=ON
# -----------------------------------------------------------------
option(USE_DEVICESETTING_PLUGIN
    "Use DeviceSettings COM-RPC plugin instead of libds/IARM for DeviceInfo" OFF)

if (USE_DEVICESETTING_PLUGIN)
    add_definitions(-DUSE_DEVICESETTING_PLUGIN)
    message(STATUS "DeviceInfo: USE_DEVICESETTING_PLUGIN=ON (COM-RPC via entservices-helpers)")
endif()

find_package(${NAMESPACE}Plugins REQUIRED)
find_package(${NAMESPACE}Definitions REQUIRED)
find_package(CompileSettingsDebug CONFIG REQUIRED)
find_package(${NAMESPACE}Helpers REQUIRED)
find_package(RFC)
find_package(IARMBus)
if (NOT USE_DEVICESETTING_PLUGIN)
    find_package(DS)
endif()

add_library(${MODULE_NAME} SHARED
        DeviceInfo.cpp
        Module.cpp)

set_target_properties(${MODULE_NAME} PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES)

target_link_libraries(${MODULE_NAME}
    PRIVATE
        CompileSettingsDebug::CompileSettingsDebug
        ${NAMESPACE}Plugins::${NAMESPACE}Plugins
        ${NAMESPACE}Definitions::${NAMESPACE}Definitions
        ${NAMESPACE}::${NAMESPACE}Helpers)

install(TARGETS ${MODULE_NAME}
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/${STORAGE_DIRECTORY}/plugins)

if (USE_DEVICESETTING_PLUGIN)
    set(IMPL_DIR DS_COMRPC)
else()
    set(IMPL_DIR DS_IARM)
endif()

add_library(${PLUGIN_IMPLEMENTATION} SHARED
    ${IMPL_DIR}/DeviceInfoImplementation.cpp
    ${IMPL_DIR}/DeviceAudioCapabilities.cpp
    ${IMPL_DIR}/DeviceVideoCapabilities.cpp
    Module.cpp)

target_include_directories(${PLUGIN_IMPLEMENTATION} PRIVATE
        ${RFC_INCLUDE_DIRS}
        ${IARMBUS_INCLUDE_DIRS})

if (NOT USE_DEVICESETTING_PLUGIN)
    target_include_directories(${PLUGIN_IMPLEMENTATION} PRIVATE
        ${DS_INCLUDE_DIRS})
endif()

set_target_properties(${PLUGIN_IMPLEMENTATION} PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES)

set_source_files_properties(
        ${IMPL_DIR}/DeviceAudioCapabilities.cpp
        ${IMPL_DIR}/DeviceVideoCapabilities.cpp
        PROPERTIES
        COMPILE_FLAGS "-fexceptions")

target_link_libraries(${PLUGIN_IMPLEMENTATION}
        PRIVATE
        CompileSettingsDebug::CompileSettingsDebug
        ${NAMESPACE}Plugins::${NAMESPACE}Plugins
        ${NAMESPACE}Definitions::${NAMESPACE}Definitions
        ${NAMESPACE}::${NAMESPACE}Helpers
        ${RFC_LIBRARIES}
        ${IARMBUS_LIBRARIES})

if (NOT USE_DEVICESETTING_PLUGIN)
    target_link_libraries(${PLUGIN_IMPLEMENTATION}
            PRIVATE
            ${DS_LIBRARIES})
endif()

if (RDK_SERVICE_L2_TEST)
   find_library(TESTMOCKLIB_LIBRARIES NAMES TestMocklib)
   if (TESTMOCKLIB_LIBRARIES)
       message ("linking mock libraries ${TESTMOCKLIB_LIBRARIES} library")
       target_link_libraries(${PLUGIN_IMPLEMENTATION} PRIVATE ${TESTMOCKLIB_LIBRARIES})
   else (TESTMOCKLIB_LIBRARIES)
       message ("Require ${TESTMOCKLIB_LIBRARIES} library")
   endif (TESTMOCKLIB_LIBRARIES)
endif (RDK_SERVICE_L2_TEST)

install(TARGETS ${PLUGIN_IMPLEMENTATION}
        DESTINATION lib/${STORAGE_DIRECTORY}/plugins)

write_config(${PLUGIN_NAME})
