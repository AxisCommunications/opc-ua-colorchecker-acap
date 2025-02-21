/**
 * Copyright (C) 2025, Axis Communications AB, Lund, Sweden
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <filesystem>
#include <format>
#include <string>
#include <sys/stat.h>

#include "CgiHandler.hpp"
#include "ColorArea.hpp"
#include "common.hpp"

#define FCGI_SOCKET_NAME "FCGI_SOCKET_NAME"

#define MIME_JSON "application/json"
#define MIME_TEXT "text/plain"

CgiHandler::CgiHandler(cv::Scalar (*GetColor)(), gboolean (*GetColorAreaValue)(), gboolean (*PickCurrentCallback)())
    : GetColor_(GetColor), GetColorAreaValue_(GetColorAreaValue), PickCurrentCallback_(PickCurrentCallback),
      running_(false)
{
    assert(nullptr != GetColor_);
    assert(nullptr != GetColorAreaValue_);
    assert(nullptr != PickCurrentCallback_);

    LOG_I("Setting up FastCGI ...");
    auto socket_path = getenv(FCGI_SOCKET_NAME);
    if (nullptr == socket_path)
    {
        LOG_E("Failed to get environment variable FCGI_SOCKET_NAME");
        assert(false);
    }

    LOG_I("Using socket path %s", socket_path);
    if (0 != FCGX_Init())
    {
        LOG_E("FCGX_Init failed");
        assert(false);
    }

    sock_ = FCGX_OpenSocket(socket_path, 5);
    chmod(socket_path, S_IRWXU | S_IRWXG | S_IRWXO);
    if (0 != FCGX_InitRequest(&request_, sock_, 0))
    {
        LOG_E("FCGX_InitRequest failed");
        assert(false);
    }

    LOG_I("Set up FastCGI for %s, start handling incoming CGI requests ...", socket_path);
    worker_ = std::jthread(&CgiHandler::Run, this);
}

CgiHandler::~CgiHandler()
{
    // std::jthread automatically joins, so we only need to set running_ to false
    LOG_I("Stop CGI handling ...");
    running_ = false;
    LOG_I("Shutting down FastCGI ...");
    close(sock_);
}

void CgiHandler::Run()
{
    running_ = true;
    while (running_)
    {
        HandleFcgiRequest();
    }
}

gboolean CgiHandler::HandleFcgiRequest()
{
    if (0 == FCGX_Accept_r(&request_))
    {
        LOG_I("FCGX_Accept_r OK");

        // Extract the CGI call only from the request
        const auto command = std::filesystem::path(FCGX_GetParam("SCRIPT_NAME", request_.envp)).filename().string();

        if ("getstatus.cgi" == command)
        {
            assert(nullptr != GetColorAreaValue_);
            const auto status_json = std::format("{{\"status\": {:b}}}", GetColorAreaValue_());
            WriteResponse(*request_.out, 200, MIME_JSON, status_json.c_str());
        }
        else if ("pickcurrent.cgi" == command)
        {
            assert(nullptr != PickCurrentCallback_);
            if (!PickCurrentCallback_())
            {
                WriteResponse(*request_.out, 500, MIME_TEXT, "Failed to pick current color");
            }
            else
            {
                const auto color = GetColor_();
                const auto color_json = std::format(R"({{"R": {}, "G": {}, "B": {}}})", color[R], color[G], color[B]);
                WriteResponse(*request_.out, 200, MIME_JSON, color_json.c_str());
            }
        }
        else
        {
            assert(nullptr != request_.out);
            WriteResponse(*request_.out, 400, MIME_TEXT, std::format("Unknown command '{}'", command).c_str());
        }

        FCGX_Finish_r(&request_);
    }
    return G_SOURCE_CONTINUE;
}

void CgiHandler::WriteResponse(FCGX_Stream &stream, const guint32 status_code, const gchar *mimetype, const gchar *msg)
{
    std::string descr;
    switch (status_code)
    {
    case 200:
        descr = "OK";
        break;
    case 400:
        descr = "Bad Request";
        break;
    case 500:
        descr = "Internal Server Error";
        break;
    default:
        LOG_E("%s/%s: Error code %u not yet implemented", __FILE__, __FUNCTION__, status_code);
        assert(false);
        break;
    }
    const std::string response_text =
        std::format("Status: {} {}\r\nContent-Type: {}\r\n\r\n{}", status_code, descr.c_str(), mimetype, msg);
    FCGX_FPrintF(&stream, response_text.c_str());
}
