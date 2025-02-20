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

#include <axhttp.h>
#include <opencv2/core/core.hpp>

class CgiHandler
{
  public:
    CgiHandler(cv::Scalar (*GetColor)(), gboolean (*GetColorAreaValue)(), gboolean (*PickCurrentCallback)());
    ~CgiHandler();
    static void RequestHandler(
        const gchar *path,
        const gchar *method,
        const gchar *query,
        GHashTable *params,
        GOutputStream *output_stream,
        gpointer user_data);

  private:
    static void
    WriteErrorResponse(GDataOutputStream &dos, const guint32 statuscode, const gchar *statusname, const gchar *msg);
    static void WriteBadRequest(GDataOutputStream &dos, const gchar *msg);
    static void WriteInternalError(GDataOutputStream &dos, const gchar *msg);

    cv::Scalar (*GetColor_)();
    gboolean (*GetColorAreaValue_)();
    gboolean (*PickCurrentCallback_)();

    AXHttpHandler *http_handler_;
};
