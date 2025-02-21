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

#pragma once

#include <fcgi_stdio.h>
#include <glib.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
#include <opencv2/core/core.hpp>
#pragma GCC diagnostic pop
#include <thread>

class CgiHandler
{
  public:
    CgiHandler(cv::Scalar (*GetColor)(), gboolean (*GetColorAreaValue)(), gboolean (*PickCurrentCallback)());
    ~CgiHandler();

  private:
    void Run();
    gboolean HandleFcgiRequest();

    static void WriteResponse(FCGX_Stream &stream, const guint32 status_code, const gchar *mimetype, const gchar *msg);

    cv::Scalar (*GetColor_)();
    gboolean (*GetColorAreaValue_)();
    gboolean (*PickCurrentCallback_)();

    FCGX_Request request_;
    bool running_;
    int sock_;
    std::jthread worker_;
};
