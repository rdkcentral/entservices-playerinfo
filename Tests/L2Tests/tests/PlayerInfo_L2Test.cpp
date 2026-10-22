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
#include <unistd.h>
#include <fstream>
#include <iostream>
#include "L2Tests.h"
#include "L2TestsMock.h"

#define JSON_TIMEOUT   (1000)
#define TEST_LOG(x, ...) fprintf(stderr, "\033[1;32m[%s:%d](%s)<PID:%d><TID:%d>" x "\n\033[0m", __FILE__, __LINE__, __FUNCTION__, getpid(), gettid(), ##__VA_ARGS__); fflush(stderr);
#define PLAYERINFO_CALLSIGN _T("PlayerInfo")
#define PLAYERINFOL2TEST_CALLSIGN _T("L2tests.1")

using namespace Thunder;
using namespace Thunder::Exchange;
using ::testing::NiceMock;

// ------------------------------------------------------------
// Test Class
// ------------------------------------------------------------
class PlayerInfo_L2test : public L2TestMocks {
protected:
    Core::JSONRPC::Message message;
    string response;
    virtual ~PlayerInfo_L2test() override;

public:
    PlayerInfo_L2test();
};

PlayerInfo_L2test::PlayerInfo_L2test() : L2TestMocks() {
    TEST_LOG("PLAYERINFO Constructor");
    uint32_t status = ActivateService(PLAYERINFO_CALLSIGN);
    EXPECT_EQ(status, Core::ERROR_NONE);
}

PlayerInfo_L2test::~PlayerInfo_L2test() {
    TEST_LOG("PLAYERINFO Destructor");
    uint32_t status = DeactivateService(PLAYERINFO_CALLSIGN);
    EXPECT_EQ(status, Core::ERROR_NONE);
}

// ------------------------------------------------------------
// AudioCodecs Test with Iterator Verification
// ------------------------------------------------------------
TEST_F(PlayerInfo_L2test, AudioCodecs_Test) {
    std::cout << "Naveen : Entering AudioCodecs test" << std::endl;

    JsonObject params;
    JsonObject result;

    uint32_t status = InvokeServiceMethod(PLAYERINFO_CALLSIGN, "audiocodecs", params, result);
    
    // Validate the JSON-RPC call was successful
    EXPECT_EQ(status, Core::ERROR_NONE);
    
    // Display what we actually received
    string resultString;
    result.ToString(resultString);
    std::cout << "Naveen : AudioCodecs JSONRPC result received: " << resultString << std::endl;

    // Validate response is not empty
    EXPECT_FALSE(resultString.empty()) << "Response should not be empty";
    EXPECT_NE(resultString, "{}") << "Response should not be empty JSON object";

    // Validate audiocodecs array property exists
    EXPECT_TRUE(result.HasLabel("audiocodecs")) << "Response should contain audiocodecs field";
    
    // Get the audiocodecs array from result
    JsonArray audioCodecsArray = result["audiocodecs"].Array();
    std::cout << "Naveen : Parsed audiocodecs array successfully" << std::endl;

    // Validate array length
    EXPECT_GT(audioCodecsArray.Length(), 0) << "audiocodecs array should not be empty";
    std::cout << "Naveen : Found " << audioCodecsArray.Length() << " audio codecs in response" << std::endl;

    // Print and validate codec names
    std::vector<string> expectedAudioCodecs = {"MPEG1", "MPEG3", "MPEG2", "MPEG4", "AAC", "AC3", "DTS", "AC3Plus", "OPUS", "VorbisOGG"};
    for (uint32_t i = 0; i < audioCodecsArray.Length(); i++) {
        string codecName = audioCodecsArray[i].String();
        std::cout << "Naveen : Audio codec [" << i << "]: " << codecName << std::endl;
        EXPECT_FALSE(codecName.empty()) << "Codec name should not be empty";
    }

    std::cout << "Naveen : Exiting AudioCodecs test" << std::endl;
}

// ------------------------------------------------------------
// VideoCodecs Test with Iterator Verification
// ------------------------------------------------------------
TEST_F(PlayerInfo_L2test, VideoCodecs_Test) {
    std::cout << "Naveen : Entering VideoCodecs test" << std::endl;

    JsonObject params;
    JsonObject result;

    uint32_t status = InvokeServiceMethod(PLAYERINFO_CALLSIGN, "videocodecs", params, result);

    // Validate the JSON-RPC call was successful
    EXPECT_EQ(status, Core::ERROR_NONE);

    // Display what we actually received
    string resultString;
    result.ToString(resultString);
    std::cout << "Naveen : VideoCodecs JSONRPC result received: " << resultString << std::endl;

    // Validate response is not empty
    EXPECT_FALSE(resultString.empty()) << "Response should not be empty";
    EXPECT_NE(resultString, "{}") << "Response should not be empty JSON object";

    // Validate videocodecs array property exists
    EXPECT_TRUE(result.HasLabel("videocodecs")) << "Response should contain videocodecs field";
    string videoCodecsString = result["videocodecs"].String();
    Core::JSON::ArrayType<Core::JSON::String> videoCodecsArray;
    videoCodecsArray.FromString(videoCodecsString);

    // Validate array length
    EXPECT_GT(videoCodecsArray.Length(), 0) << "videocodecs array should not be empty";
    std::cout << "Naveen : Found " << videoCodecsArray.Length() << " video codecs in response" << std::endl;

    // Print and validate codec names
    std::vector<string> expectedVideoCodecs = {"H263", "H264", "H265", "MPEG", "VP8", "VP9", "VP10"};
    for (uint32_t i = 0; i < videoCodecsArray.Length(); i++) {
        string codecName = videoCodecsArray[i].Value();
        std::cout << "Naveen : Video codec [" << i << "]: " << codecName << std::endl;
        EXPECT_FALSE(codecName.empty()) << "Codec name should not be empty";
    }

    std::cout << "Naveen : Exiting VideoCodecs test" << std::endl;
}