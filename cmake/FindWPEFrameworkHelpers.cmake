# If not stated otherwise in this file or this component's license file the
# following copyright and licenses apply:
#
# Copyright 2024 RDK Management
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

# - Try to find ThunderHelpers
# Once done this will define
#  ThunderHelpers_FOUND        - System has ThunderHelpers
#  ThunderHelpers_INCLUDE_DIRS - The ThunderHelpers include directories
#  ThunderHelpers_LIBRARIES    - The libraries needed to use ThunderHelpers
#
# Also creates an imported target:
#  Thunder::ThunderHelpers

find_library(ThunderHelpers_LIBRARIES
    NAMES ThunderHelpers
    PATH_SUFFIXES thunder/plugins)

find_path(ThunderHelpers_INCLUDE_DIRS
    NAMES UtilsLogging.h
    PATH_SUFFIXES thunder/helpers)

set(ThunderHelpers_LIBRARIES    ${ThunderHelpers_LIBRARIES}    CACHE PATH "Path to ThunderHelpers library")
set(ThunderHelpers_INCLUDE_DIRS ${ThunderHelpers_INCLUDE_DIRS} CACHE PATH "Path to ThunderHelpers includes")

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(ThunderHelpers DEFAULT_MSG
    ThunderHelpers_INCLUDE_DIRS
    ThunderHelpers_LIBRARIES)

if(ThunderHelpers_FOUND AND NOT TARGET Thunder::ThunderHelpers)
    add_library(Thunder::ThunderHelpers SHARED IMPORTED)
    set_target_properties(Thunder::ThunderHelpers PROPERTIES
        IMPORTED_LOCATION             "${ThunderHelpers_LIBRARIES}"
        INTERFACE_INCLUDE_DIRECTORIES "${ThunderHelpers_INCLUDE_DIRS}")
endif()

mark_as_advanced(
    ThunderHelpers_FOUND
    ThunderHelpers_INCLUDE_DIRS
    ThunderHelpers_LIBRARIES)
