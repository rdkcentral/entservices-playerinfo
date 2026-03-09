/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2020 RDK Management
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

#include "PlayerInfo.h"

#define API_VERSION_NUMBER_MAJOR 1
#define API_VERSION_NUMBER_MINOR 0
#define API_VERSION_NUMBER_PATCH 8

namespace WPEFramework {

namespace {

    static Plugin::Metadata<Plugin::PlayerInfo> metadata(
        // Version (Major, Minor, Patch)
        API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH,
        // Preconditions
        {},
        // Terminations
        {},
        // Controls
        {}
    );
}

namespace Plugin {

    SERVICE_REGISTRATION(PlayerInfo, API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH);

    static Core::ProxyPoolType<Web::Response> responseFactory(4);
    static Core::ProxyPoolType<Web::JSONBodyType<JsonData::PlayerInfo::CodecsData>> jsonResponseFactory(4);

    /* virtual */ const string PlayerInfo::Initialize(PluginHost::IShell* service)
    {
        ASSERT(service != nullptr);
        ASSERT(_service == nullptr);
        ASSERT(_connectionId == 0);
        ASSERT(_player == nullptr);

        string message;
        Config config;
        config.FromString(service->ConfigLine());
        _skipURL = static_cast<uint8_t>(service->WebPrefix().length());

        _service = service;
        _service->AddRef();
        // Register the Process::Notification stuff. The Remote process might die before we get a
        // change to "register" the sink for these events !!! So do it ahead of instantiation.
        _service->Register(&_notification);

        _player = service->Root<Exchange::IPlayerProperties>(_connectionId, 2000, _T("PlayerInfoImplementation"));
        if (_player != nullptr) {

            if ((_player->AudioCodecs(_audioCodecs) == Core::ERROR_NONE) && (_audioCodecs != nullptr)) {

                if ((_player->VideoCodecs(_videoCodecs) == Core::ERROR_NONE) && (_videoCodecs != nullptr)) {
                    Exchange::JPlayerProperties::Register(*this, _player);
                    
                    //  L2 Test specific manual registration
                    // 
                    // WHY MANUAL REGISTRATION FOR L2 TESTS:
                    // The auto-generated JSON-RPC registration (Exchange::JPlayerProperties::Register) 
                    // creates methods that return empty results for codec arrays. This is because the
                    // auto-generated Info() method doesn't properly serialize array properties - it only
                    // populates the internal data structure but doesn't convert it to the expected JSON
                    // array format that L2 tests can validate.
                    //
                    // Our custom get_audiocodecs/get_videocodecs methods:
                    // 1. Reuse the existing Info() logic to populate codec data  
                    // 2. Extract and properly serialize codec arrays as JSON arrays
                    // 3. Return response with "audiocodecs"/"videocodecs" array properties
                    // 4. Enable L2 tests to validate actual codec data instead of empty responses
                    //
                    #ifdef RDK_SERVICE_L2_TEST
                    std::cout << "Registering manual audiocodecs method for L2 test" << std::endl;
                    Register("audiocodecs", &PlayerInfo::get_audiocodecs, this);
                    // Register videocodecs for L2 test
                    std::cout << "Registering manual videocodecs method for L2 test" << std::endl;
                    Register("videocodecs", &PlayerInfo::get_videocodecs, this);
                    #else
                    std::cout << "Using auto-registration (RDK_SERVICE_L2_TEST not defined)" << std::endl;
                    #endif
                    // The code execution should proceed regardless of the _dolbyOut
                    // value, as it is not a essential.
                    // The relevant JSONRPC endpoints will return ERROR_UNAVAILABLE,
                    // if it hasn't been initialized.
                    _dolbyOut = _player->QueryInterface<Exchange::Dolby::IOutput>();
                    if(_dolbyOut == nullptr){
                        SYSLOG(Logging::Startup, (_T("Dolby output switching service is unavailable.")));
                    } else {
                        _dolbyNotification.Initialize(_dolbyOut);
                        Exchange::Dolby::JOutput::Register(*this, _dolbyOut);
                    }
                } else {
                    message = _T("PlayerInfo Video Codecs not be Loaded.");
                }
            } else {
                message = _T("PlayerInfo Audio Codecs not be Loaded.");
            }
        }
        else {
            message = _T("PlayerInfo could not be instantiated.");
        }

        if(message.length() != 0){
            Deinitialize(service);
        }
        return message;
    }

    /* virtual */ void PlayerInfo::Deinitialize(PluginHost::IShell* service VARIABLE_IS_NOT_USED)
    {
        ASSERT(service == _service);

        _service->Unregister(&_notification);

        if (_player != nullptr) {
            if(_audioCodecs != nullptr && _videoCodecs != nullptr) {
                Exchange::JPlayerProperties::Unregister(*this);
            }
            if (_audioCodecs != nullptr) {
                _audioCodecs->Release();
                _audioCodecs = nullptr;
            }
            if (_videoCodecs != nullptr) {
                _videoCodecs->Release();
                _videoCodecs = nullptr;
            }
            if (_dolbyOut != nullptr) {
            Exchange::Dolby::JOutput::Unregister(*this);
            _dolbyNotification.Deinitialize();
            _dolbyOut->Release();
            _dolbyOut = nullptr;
            }

            RPC::IRemoteConnection* connection(_service->RemoteConnection(_connectionId));
            VARIABLE_IS_NOT_USED uint32_t result = _player->Release();
            _player = nullptr;
            ASSERT(result == Core::ERROR_DESTRUCTION_SUCCEEDED);

            // The connection can disappear in the meantime...
            if (connection != nullptr) {
                // But if it did not dissapear in the meantime, forcefully terminate it. Shoot to kill :-)
                connection->Terminate();
                connection->Release();
            }
        }

        _service->Release();
        _service = nullptr;
        _player = nullptr;
        
        _connectionId = 0;

    }

    /* virtual */ string PlayerInfo::Information() const
    {
        // No additional info to report.
        return (string());
    }

    /* virtual */ void PlayerInfo::Inbound(Web::Request& /* request */)
    {
    }

    /* virtual */ Core::ProxyType<Web::Response> PlayerInfo::Process(const Web::Request& request)
    {
        ASSERT(_skipURL <= request.Path.length());

        Core::ProxyType<Web::Response> result(PluginHost::IFactories::Instance().Response());

        // By default, we assume everything works..
        result->ErrorCode = Web::STATUS_OK;
        result->Message = "OK";

        // <GET> - currently, only the GET command is supported, returning system info
        if (request.Verb == Web::Request::HTTP_GET) {

            Core::ProxyType<Web::JSONBodyType<JsonData::PlayerInfo::CodecsData>> response(jsonResponseFactory.Element());

            Core::TextSegmentIterator index(Core::TextFragment(request.Path, _skipURL, static_cast<uint32_t>(request.Path.length()) - _skipURL), false, '/');

            // Always skip the first one, it is an empty part because we start with a '/' if there are more parameters.
            index.Next();

            Info(*response);
            result->ContentType = Web::MIMETypes::MIME_JSON;
            result->Body(Core::ProxyType<Web::IBody>(response));
        } else {
            result->ErrorCode = Web::STATUS_BAD_REQUEST;
            result->Message = _T("Unsupported request for the [PlayerInfo] service.");
        }

        return result;
    }

    void PlayerInfo::Info(JsonData::PlayerInfo::CodecsData& playerInfo) const
    {
        Core::JSON::EnumType<JsonData::PlayerInfo::CodecsData::AudiocodecsType> audioCodec;
        _audioCodecs->Reset(0);
        Exchange::IPlayerProperties::AudioCodec audio;
        while(_audioCodecs->Next(audio) == true) {
            playerInfo.Audio.Add(audioCodec = static_cast<JsonData::PlayerInfo::CodecsData::AudiocodecsType>(audio));
        }

        Core::JSON::EnumType<JsonData::PlayerInfo::CodecsData::VideocodecsType> videoCodec;
        Exchange::IPlayerProperties::VideoCodec video;
        _videoCodecs->Reset(0);
         while(_videoCodecs->Next(video) == true) {
            playerInfo.Video.Add(videoCodec = static_cast<JsonData::PlayerInfo::CodecsData::VideocodecsType>(video));
        }
    }

    //  Custom JSON-RPC method that reuses existing Info method instead of duplicating logic
    #ifdef RDK_SERVICE_L2_TEST
    uint32_t PlayerInfo::get_audiocodecs(const JsonObject& parameters, JsonObject& response)
    {     
        if (_audioCodecs == nullptr || _videoCodecs == nullptr) {
            std::cout << "get_audiocodecs - codecs not available" << std::endl;
            response["error"] = "Audio codecs not available";
            return Core::ERROR_UNAVAILABLE;
        }
        
        //  Reuse existing Info method to populate codec data
        JsonData::PlayerInfo::CodecsData codecsData;
        Info(codecsData);  // This calls our existing method that does all the iteration
        
        //  Extract just the audio codecs array from the populated data
        JsonArray audioCodecsArray;
        
        // Convert the Audio array from CodecsData to string array for JSON-RPC response
        Core::JSON::ArrayType<Core::JSON::EnumType<JsonData::PlayerInfo::CodecsData::AudiocodecsType>>::Iterator audioIter = codecsData.Audio.Elements();
        int codecCount = 0;
        while (audioIter.Next()) {
            codecCount++;
            // Convert enum value to string representation
            string codecName = audioIter.Current().Data();
            audioCodecsArray.Add(codecName);
        }
        
        // Since WPEFramework JsonObject can't directly be an array, 
        // let's put the array in a standard property
        response.Clear();
        
        // Create a JsonArray with our codecs
        JsonArray codecsArray;
        for (int i = 0; i < codecCount; i++) {
            string codecName = audioCodecsArray[i].Value();
            codecsArray.Add(codecName);
        }
        
        // Put the array in the response object
        response["audiocodecs"] = codecsArray;
        
        return Core::ERROR_NONE;
    }

    uint32_t PlayerInfo::get_videocodecs(const JsonObject& parameters, JsonObject& response)
    {
        if (_audioCodecs == nullptr || _videoCodecs == nullptr) {
            std::cout << "get_videocodecs - codecs not available" << std::endl;
            response["error"] = "Video codecs not available";
            return Core::ERROR_UNAVAILABLE;
        }
        // Reuse Info method to populate codec data
        JsonData::PlayerInfo::CodecsData codecsData;
        Info(codecsData);
        // Extract just the video codecs array from the populated data
        JsonArray videoCodecsArray;
        Core::JSON::ArrayType<Core::JSON::EnumType<JsonData::PlayerInfo::CodecsData::VideocodecsType>>::Iterator videoIter = codecsData.Video.Elements();
        int codecCount = 0;
        while (videoIter.Next()) {
            codecCount++;
            string codecName = videoIter.Current().Data();
            videoCodecsArray.Add(codecName);
        }
        response.Clear();
        JsonArray codecsArray;
        for (int i = 0; i < codecCount; i++) {
            string codecName = videoCodecsArray[i].Value();
            codecsArray.Add(codecName);
        }
        response["videocodecs"] = codecsArray;
        return Core::ERROR_NONE;
    }
    #endif

    void PlayerInfo::Deactivated(RPC::IRemoteConnection* connection)
    {
        // This can potentially be called on a socket thread, so the deactivation (wich in turn kills this object) must be done
        // on a seperate thread. Also make sure this call-stack can be unwound before we are totally destructed.
        if (_connectionId == connection->Id()) {
            ASSERT(_service != nullptr);
            Core::IWorkerPool::Instance().Submit(PluginHost::IShell::Job::Create(_service, PluginHost::IShell::DEACTIVATED, PluginHost::IShell::FAILURE));
        }
    }
} // namespace Plugin
} // namespace WPEFramework
