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

#include <axparameter.h>
#include <opencv2/core/core.hpp>

class ParamHandler
{
  public:
    ParamHandler(const gchar *app_name, void (*PurgeColorArea)(), void (*RestartOpcUaServer)(unsigned int));
    ~ParamHandler();
    gboolean SetColor(const cv::Scalar color);
    gboolean SetResolution(const gint32 w, const gint32 h);

    static void ParamCallbackDouble(const gchar *name, const gchar *value, void *data);
    static void ParamCallbackInt(const gchar *name, const gchar *value, void *data);

    cv::Point GetCenterPoint() const
    {
        return center_point_;
    };
    cv::Scalar GetColor() const
    {
        return color_;
    };
    guint32 GetMarkerWidth() const
    {
        return markerwidth_;
    };
    guint32 GetMarkerHeight() const
    {
        return markerheight_;
    };
    guint8 GetMarkerShape() const
    {
        return markershape_;
    };
    guint8 GetTolerance() const
    {
        return tolerance_;
    };

  private:
    gboolean SetParam(const gchar *name, const gchar &value, gboolean do_sync);
    gboolean SetParam(const gchar *name, const gdouble value, gboolean do_sync);
    gboolean SetParam(const gchar *name, const gint32 value, gboolean do_sync);
    gchar *GetParam(const gchar *name) const;
    gboolean GetParam(const gchar *name, gdouble &val) const;
    gboolean GetParam(const gchar *name, gint32 &val) const;
    void UpdateLocalParam(const gchar *name, const gdouble val);
    void UpdateLocalParam(const gchar *name, const gint32 val);
    gboolean SetupParam(const gchar *name, AXParameterCallback callbackfn);
    gboolean SetupParamDouble(const gchar *name, AXParameterCallback callbackfn);
    gboolean SetupParamInt(const gchar *name, AXParameterCallback callbackfn);

    void (*PurgeColorArea_)();
    void (*RestartOpcUaServer_)(const guint32);

    AXParameter *axparameter_;
    cv::Point center_point_;
    cv::Scalar color_;
    guint32 markerwidth_;
    guint32 markerheight_;
    guint8 markershape_;
    guint8 tolerance_;
    mutable GMutex mtx_;
};
