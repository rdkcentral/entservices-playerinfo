/**
 * If not stated otherwise in this file or this component's LICENSE
 * file the following copyright and licenses apply:
 *
 * Copyright 2021 RDK Management
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

#pragma once

#include <stdio.h>
#include <string.h>

typedef enum profile {
    PROFILE_INVALID = -1,
    PROFILE_STB     = 0,
    PROFILE_TV,
    PROFILE_MAX
} profile_t;

/* Shorthand aliases used in entservices C++ plugins */
static const profile_t TV  = PROFILE_TV;
static const profile_t STB = PROFILE_STB;

#define RDK_PROFILE     "RDK_PROFILE"
#define PROFILE_STR_TV  "TV"
#define PROFILE_STR_STB "STB"

inline profile_t searchRdkProfile(void)
{
    const char* devPropPath = "/etc/device.properties";
    char line[256], *rdkProfile = NULL;
    profile_t ret = PROFILE_INVALID;
    FILE* file;

    file = fopen(devPropPath, "r");
    if (file == NULL) {
        printf("[searchRdkProfile]: File not found.\n");
        return PROFILE_INVALID;
    }

    while (fgets(line, sizeof(line), file)) {
        rdkProfile = strstr(line, RDK_PROFILE);
        if (rdkProfile != NULL) {
            break;
        }
    }

    if (rdkProfile != NULL) {
        rdkProfile += strlen(RDK_PROFILE);
        rdkProfile++; /* Move past the '=' character */
        if (0 == strncmp(rdkProfile, PROFILE_STR_TV, strlen(PROFILE_STR_TV))) {
            ret = PROFILE_TV;
        } else if (0 == strncmp(rdkProfile, PROFILE_STR_STB, strlen(PROFILE_STR_STB))) {
            ret = PROFILE_STB;
        }
    } else {
        printf("[searchRdkProfile]: NOT FOUND RDK_PROFILE in device properties file\n");
        ret = PROFILE_INVALID;
    }

    fclose(file);
    return ret;
}
