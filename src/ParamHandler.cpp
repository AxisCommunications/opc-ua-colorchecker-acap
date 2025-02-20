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
#include <unistd.h>

#include "ColorArea.hpp"
#include "ParamHandler.hpp"
#include "common.hpp"

using namespace cv;
using namespace std;

ParamHandler::ParamHandler(const gchar *app_name, void (*PurgeColorArea)(), void (*RestartOpcUaServer)(const guint32))
    : PurgeColorArea_(PurgeColorArea), RestartOpcUaServer_(RestartOpcUaServer), center_point_(0, 0), color_(0, 0),
      markerwidth_(0), markerheight_(0), markershape_(0), tolerance_(0)
{
    assert(nullptr != PurgeColorArea_);
    assert(nullptr != RestartOpcUaServer_);

    LOG_I("Init parameter handling ...");
    g_mutex_init(&mtx_);
    GError *error = nullptr;
    axparameter_ = ax_parameter_new(app_name, &error);
    if (nullptr != error)
    {
        LOG_E("%s/%s: ax_parameter_new failed (%s)", __FILE__, __FUNCTION__, error->message);
        g_error_free(error);
        assert(FALSE);
    }
    assert(nullptr != axparameter_);
    // clang-format off
    LOG_I("Setting up parameters ...");
    if (!SetupParamInt("CenterX", ParamCallbackInt) ||
        !SetupParamInt("CenterY", ParamCallbackInt) ||
        !SetupParamDouble("ColorB", ParamCallbackDouble) ||
        !SetupParamDouble("ColorG", ParamCallbackDouble) ||
        !SetupParamDouble("ColorR", ParamCallbackDouble) ||
        !SetupParamInt("Height", ParamCallbackInt) ||
        !SetupParamInt("MarkerHeight", ParamCallbackInt) ||
        !SetupParamInt("MarkerShape", ParamCallbackInt) ||
        !SetupParamInt("MarkerWidth", ParamCallbackInt) ||
        !SetupParamInt("Port", ParamCallbackInt) ||
        !SetupParamInt("Tolerance", ParamCallbackInt) ||
        !SetupParamInt("Width", ParamCallbackInt))
    // clang-format on
    {
        LOG_E("%s/%s: Failed to set up parameters", __FILE__, __FUNCTION__);
        assert(FALSE);
    }

    // Log retrieved param values
    LOG_I("%s/%s: center: (%u, %u)", __FILE__, __FUNCTION__, center_point_.x, center_point_.y);
    LOG_I(
        "%s/%s: color (R, G, B) = (%.1f, %.1f, %.1f)",
        __FILE__,
        __FUNCTION__,
        color_.val[R],
        color_.val[G],
        color_.val[B]);
    LOG_I("%s/%s: marker dimenstions (w, h) = (%u, %u)", __FILE__, __FUNCTION__, markerwidth_, markerheight_);
    LOG_I("%s/%s: marker shape = %u", __FILE__, __FUNCTION__, markershape_);
    LOG_I("%s/%s: tolerance: %u", __FILE__, __FUNCTION__, tolerance_);
}

ParamHandler::~ParamHandler()
{
    assert(nullptr != axparameter_);
    ax_parameter_free(axparameter_);
    g_mutex_clear(&mtx_);
}

gboolean ParamHandler::SetColor(const cv::Scalar color)
{
    g_mutex_lock(&mtx_);
    color_ = color;
    const auto result = SetParam("ColorB", static_cast<gdouble>(color.val[B]), FALSE) &&
                        SetParam("ColorG", static_cast<gdouble>(color.val[G]), FALSE) &&
                        SetParam("ColorR", static_cast<gdouble>(color.val[R]), TRUE);
    g_mutex_unlock(&mtx_);

    return result;
}

gboolean ParamHandler::SetResolution(const gint32 w, const gint32 h)
{
    g_mutex_lock(&mtx_);
    const auto result = (SetParam("Width", w, FALSE) && SetParam("Height", h, TRUE));
    g_mutex_unlock(&mtx_);

    return result;
}

void ParamHandler::ParamCallbackDouble(const gchar *name, const gchar *value, void *data)
{
    assert(nullptr != name);
    assert(nullptr != value);
    assert(nullptr != data);
    if (nullptr == value)
    {
        LOG_E("%s/%s: Unexpected nullptr value for %s", __FILE__, __FUNCTION__, name);
        return;
    }

    LOG_I("Update for parameter %s (%s)", name, value);
    const auto lastdot = strrchr(name, '.');
    assert(nullptr != lastdot);
    assert(1 < strlen(name) - strlen(lastdot));
    auto param_handler = static_cast<ParamHandler *>(data);
    param_handler->UpdateLocalParam(&lastdot[1], static_cast<gdouble>(atof(value)));
}

void ParamHandler::ParamCallbackInt(const gchar *name, const gchar *value, void *data)
{
    assert(nullptr != name);
    assert(nullptr != value);
    assert(nullptr != data);
    if (nullptr == value)
    {
        LOG_E("%s/%s: Unexpected nullptr value for %s", __FILE__, __FUNCTION__, name);
        return;
    }

    LOG_I("Update for parameter %s (%s)", name, value);
    const auto lastdot = strrchr(name, '.');
    assert(nullptr != lastdot);
    assert(1 < strlen(name) - strlen(lastdot));
    auto param_handler = static_cast<ParamHandler *>(data);
    param_handler->UpdateLocalParam(&lastdot[1], static_cast<gint32>(atoi(value)));
}

gboolean ParamHandler::SetParam(const gchar *name, const gchar &value, gboolean do_sync = TRUE)
{
    assert(nullptr != axparameter_);
    GError *error = nullptr;

    if (!ax_parameter_set(axparameter_, name, &value, do_sync, &error))
    {
        LOG_E("%s/%s: failed to set %s parameter", __FILE__, __FUNCTION__, name);
        if (nullptr != error)
        {
            LOG_E("%s/%s: %s", __FILE__, __FUNCTION__, error->message);
            g_error_free(error);
        }
        return FALSE;
    }
    LOG_I("Set %s value: %s", name, &value);
    return TRUE;
}

gboolean ParamHandler::SetParam(const gchar *name, const gdouble value, gboolean do_sync = TRUE)
{
    const auto valuestr = g_strdup_printf("%f", value);
    assert(nullptr != valuestr);
    const auto result = SetParam(name, *valuestr, do_sync);
    g_free(valuestr);
    return result;
}

gboolean ParamHandler::SetParam(const gchar *name, const gint32 value, gboolean do_sync = TRUE)
{
    const auto valuestr = g_strdup_printf("%d", value);
    assert(nullptr != valuestr);
    const auto result = SetParam(name, *valuestr, do_sync);
    g_free(valuestr);
    return result;
}

gchar *ParamHandler::GetParam(const gchar *name) const
{
    assert(nullptr != axparameter_);
    GError *error = nullptr;
    gchar *value = nullptr;
    if (!ax_parameter_get(axparameter_, name, &value, &error))
    {
        LOG_E("%s/%s: failed to get %s parameter", __FILE__, __FUNCTION__, name);
        if (nullptr != error)
        {
            LOG_E("%s/%s: %s", __FILE__, __FUNCTION__, error->message);
            g_error_free(error);
        }
        return nullptr;
    }
    LOG_I("Got %s value: %s", name, value);
    return value;
}

gboolean ParamHandler::GetParam(const gchar *name, gdouble &val) const
{
    const auto valuestr = GetParam(name);
    if (nullptr == valuestr)
    {
        return FALSE;
    }
    val = atof(valuestr);
    g_free(valuestr);
    return TRUE;
}

gboolean ParamHandler::GetParam(const gchar *name, gint32 &val) const
{
    const auto valuestr = GetParam(name);
    if (nullptr == valuestr)
    {
        return FALSE;
    }
    val = atoi(valuestr);
    g_free(valuestr);
    return TRUE;
}

void ParamHandler::UpdateLocalParam(const gchar *name, const gdouble val)
{
    g_mutex_lock(&mtx_);
    if (0 == strcmp("ColorR", name))
    {
        color_.val[R] = val;
    }
    else if (0 == strcmp("ColorG", name))
    {
        color_.val[G] = val;
    }
    else if (0 == strcmp("ColorB", name))
    {
        color_.val[B] = val;
    }
    else
    {
        LOG_E("%s/%s: FAILED to act on param %s", __FILE__, __FUNCTION__, name);
        throw runtime_error("Unknown double parameter.");
    }
    PurgeColorArea_();
    g_mutex_unlock(&mtx_);
}

void ParamHandler::UpdateLocalParam(const gchar *name, const gint32 val)
{
    // Parameters that do not change the color checker go here
    if (0 == strncmp("Port", name, 4))
    {
        assert(nullptr != RestartOpcUaServer_);
        RestartOpcUaServer_(val);
        return;
    }
    if (0 == strncmp("Width", name, 5) || 0 == strncmp("Height", name, 5))
    {
        // These values are not to be set by the user but only read by the config UI
        return;
    }

    // The following parameters trigger recalibration of the color area
    g_mutex_lock(&mtx_);
    if (0 == strcmp("CenterX", name))
    {
        center_point_.x = val;
    }
    else if (0 == strcmp("CenterY", name))
    {
        center_point_.y = val;
    }
    else if (0 == strcmp("MarkerWidth", name))
    {
        markerwidth_ = val;
    }
    else if (0 == strcmp("MarkerHeight", name))
    {
        markerheight_ = val;
    }
    else if (0 == strcmp("MarkerShape", name))
    {
        markershape_ = val;
    }
    else if (0 == strcmp("Tolerance", name))
    {
        tolerance_ = val;
    }
    else
    {
        LOG_E("%s/%s: FAILED to act on param %s", __FILE__, __FUNCTION__, name);
        throw runtime_error("Unknown int parameter.");
    }

    PurgeColorArea_();
    g_mutex_unlock(&mtx_);
}

gboolean ParamHandler::SetupParam(const gchar *name, AXParameterCallback callbackfn)
{
    assert(nullptr != axparameter_);
    GError *error = nullptr;

    assert(nullptr != name);
    assert(nullptr != callbackfn);

    if (!ax_parameter_register_callback(axparameter_, name, callbackfn, this, &error))
    {
        LOG_E("%s/%s: failed to register %s callback", __FILE__, __FUNCTION__, name);
        if (nullptr != error)
        {
            LOG_E("%s/%s: %s", __FILE__, __FUNCTION__, error->message);
            g_error_free(error);
        }
        return FALSE;
    }

    return TRUE;
}

gboolean ParamHandler::SetupParamDouble(const gchar *name, AXParameterCallback callbackfn)
{
    if (!SetupParam(name, callbackfn))
    {
        return FALSE;
    }

    gdouble val;
    if (!GetParam(name, val))
    {
        LOG_E("%s/%s: Failed to get initial value for %s", __FILE__, __FUNCTION__, name);
        return FALSE;
    }
    UpdateLocalParam(name, val);
    LOG_I("%s/%s: Set up double parameter %s", __FILE__, __FUNCTION__, name);
    usleep(50000); // mitigate timing issue in parameter handling

    return TRUE;
}

gboolean ParamHandler::SetupParamInt(const gchar *name, AXParameterCallback callbackfn)
{
    if (!SetupParam(name, callbackfn))
    {
        return FALSE;
    }

    gint32 val;
    if (!GetParam(name, val))
    {
        LOG_E("%s/%s: Failed to get initial value for %s", __FILE__, __FUNCTION__, name);
        return FALSE;
    }
    UpdateLocalParam(name, val);
    LOG_I("%s/%s: Set up integer parameter %s", __FILE__, __FUNCTION__, name);
    usleep(50000); // mitigate timing issue in parameter handling

    return TRUE;
}
