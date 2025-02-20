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
#include <ostream>
#include <sstream>
#include <string>

#include "CgiHandler.hpp"
#include "ColorArea.hpp"
#include "common.hpp"

using namespace std;

CgiHandler::CgiHandler(cv::Scalar (*GetColor)(), gboolean (*GetColorAreaValue)(), gboolean (*PickCurrentCallback)())
    : GetColor_(GetColor), GetColorAreaValue_(GetColorAreaValue), PickCurrentCallback_(PickCurrentCallback),
      http_handler_(ax_http_handler_new(this->RequestHandler, this))
{
    assert(nullptr != GetColor_);
    assert(nullptr != GetColorAreaValue_);
    assert(nullptr != PickCurrentCallback_);
    assert(nullptr != http_handler_);
}

CgiHandler::~CgiHandler()
{
    LOG_I("%s/%s: Free http handler ...", __FILE__, __FUNCTION__);
    assert(nullptr != http_handler_);
    ax_http_handler_free(http_handler_);
}

void CgiHandler::WriteErrorResponse(
    GDataOutputStream &dos,
    const guint32 statuscode,
    const gchar *statusname,
    const gchar *msg)
{
    ostringstream ss;
    ss << "Status: " << statuscode << " " << statusname << "\r\n"
       << "Content-Type: text/html\r\n"
       << "\r\n"
       << "<HTML><HEAD><TITLE>" << statuscode << " " << statusname << "</TITLE></HEAD>\n"
       << "<BODY><H1>" << statuscode << " " << statusname << "</H1>\n"
       << msg << "\n"
       << "</BODY></HTML>\n";

    g_data_output_stream_put_string(&dos, ss.str().c_str(), nullptr, nullptr);
}

void CgiHandler::WriteBadRequest(GDataOutputStream &dos, const gchar *msg)
{
    WriteErrorResponse(dos, 400, "Bad Request", msg);
}

void CgiHandler::WriteInternalError(GDataOutputStream &dos, const gchar *msg)
{
    WriteErrorResponse(dos, 500, "Internal Server Error", msg);
}

void CgiHandler::RequestHandler(
    const gchar *path,
    const gchar *method,
    const gchar *query,
    GHashTable *params,
    GOutputStream *output_stream,
    gpointer user_data)
{
    (void)method;
    (void)query;
    (void)params;

    auto dos = g_data_output_stream_new(output_stream);
    assert(nullptr != dos);
    assert(nullptr != user_data);
    auto cgi_handler = static_cast<CgiHandler *>(user_data);

    const auto func = basename(const_cast<char *>(path));
    if (0 == strcmp("getstatus.cgi", func))
    {
        assert(nullptr != cgi_handler->GetColorAreaValue_);
        const auto status = cgi_handler->GetColorAreaValue_();
        g_data_output_stream_put_string(dos, "Status: 200 OK\r\n", nullptr, nullptr);
        g_data_output_stream_put_string(dos, "Content-Type: application/json\r\n\r\n", nullptr, nullptr);
        ostringstream ss;
        ss << "{\"status\": " << (status ? "true" : "false") << "}" << endl;
        g_data_output_stream_put_string(dos, ss.str().c_str(), nullptr, nullptr);
    }
    else if (0 == strcmp("pickcurrent.cgi", func))
    {
        assert(nullptr != cgi_handler->PickCurrentCallback_);
        if (!cgi_handler->PickCurrentCallback_())
        {
            WriteInternalError(*dos, "Failed to pick current color");
            goto http_exit;
        }

        const auto color = cgi_handler->GetColor_();
        g_data_output_stream_put_string(dos, "Status: 200 OK\r\n", nullptr, nullptr);
        g_data_output_stream_put_string(dos, "Content-Type: application/json\r\n\r\n", nullptr, nullptr);
        ostringstream ss;
        ss << "{\"R\":" << color[R] << ", \"G\":" << color[G] << ", \"B\":" << color[B] << "}" << endl;
        g_data_output_stream_put_string(dos, ss.str().c_str(), nullptr, nullptr);
    }
    else
    {
        WriteBadRequest(*dos, "Unknown action");
    }
http_exit:
    g_object_unref(dos);
}
