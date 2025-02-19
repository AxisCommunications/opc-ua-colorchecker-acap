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

#include <atomic>
#include <axparameter.h>
#include <mutex>
#include <opencv2/imgproc.hpp>
#include <opencv2/video.hpp>
#include <stdexcept>
#include <syslog.h>

#include "CgiHandler.hpp"
#include "ColorArea.hpp"
#include "EventHandler.hpp"
#include "ImageProvider.hpp"
#include "OpcUaServer.hpp"
#include "common.hpp"

using namespace cv;
using namespace std;

enum MarkerShape
{
    Ellipse = 0,
    Rectangle,
    MarkerCount
};

static atomic<bool> pickcurrent(false);
static atomic<bool> currentstate(false);

static GMainLoop *loop = nullptr;

static mutex mtx;

static EventHandler evhandler;
static AXParameter *axparameter = nullptr;
static Point center_point;
static Scalar color;
static uint32_t markerwidth;
static uint32_t markerheight;
static uint8_t markershape;
static uint8_t tolerance;
static ColorArea *colorarea = nullptr;
static OpcUaServer opcuaserver;

static ImageProvider *provider = nullptr;
static Mat nv12_mat;

static gboolean set_param(const gchar *name, const gchar &value, gboolean do_sync = TRUE)
{
    assert(nullptr != axparameter);
    GError *error = nullptr;

    if (!ax_parameter_set(axparameter, name, &value, do_sync, &error))
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

static gboolean set_param_double(const gchar *name, const double value, gboolean do_sync = TRUE)
{
    const auto valuestr = g_strdup_printf("%f", value);
    assert(nullptr != valuestr);
    gboolean result = set_param(name, *valuestr, do_sync);
    g_free(valuestr);
    return result;
}

static gchar *get_param(const gchar *name)
{
    assert(nullptr != axparameter);
    GError *error = nullptr;
    gchar *value = nullptr;
    if (!ax_parameter_get(axparameter, name, &value, &error))
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

static gboolean get_param_double(const gchar *name, double &val)
{
    const auto valuestr = get_param(name);
    if (nullptr == valuestr)
    {
        return FALSE;
    }
    val = atof(valuestr);
    g_free(valuestr);
    return TRUE;
}

static gboolean get_param_int(const gchar *name, int &val)
{
    const auto valuestr = get_param(name);
    if (nullptr == valuestr)
    {
        return FALSE;
    }
    val = atoi(valuestr);
    g_free(valuestr);
    return TRUE;
}

static void purge_colorarea(void)
{
    // Recalibrate color checker at next frame
    if (nullptr != colorarea)
    {
        delete colorarea;
        colorarea = nullptr;
    }
}

static void update_local_param_double(const gchar &name, const double val)
{
    mtx.lock();
    if (0 == strcmp("ColorR", &name))
    {
        color.val[R] = val;
    }
    else if (0 == strcmp("ColorG", &name))
    {
        color.val[G] = val;
    }
    else if (0 == strcmp("ColorB", &name))
    {
        color.val[B] = val;
    }
    else
    {
        LOG_E("%s/%s: FAILED to act on param %s", __FILE__, __FUNCTION__, &name);
        throw runtime_error("Unknown double parameter.");
    }

    purge_colorarea();
    mtx.unlock();
}

static void update_local_param_int(const gchar &name, const uint32_t val)
{
    // Parameters that do not change the color checker go here
    if (0 == strncmp("Port", &name, 4))
    {
        if (opcuaserver.IsRunning())
        {
            opcuaserver.ShutDownServer();
        }
        if (!opcuaserver.LaunchServer(val))
        {
            const char *msg = "Failed to launch OPC UA server";
            LOG_E("%s/%s: %s", __FILE__, __FUNCTION__, msg);
            throw runtime_error(msg);
        }
        return;
    }
    if (0 == strncmp("Width", &name, 5) || 0 == strncmp("Height", &name, 5))
    {
        // These values are not to be set by the user but only read by the config UI
        return;
    }

    // The following parameters trigger recalibration of the color area
    mtx.lock();
    if (0 == strcmp("CenterX", &name))
    {
        center_point.x = val;
    }
    else if (0 == strcmp("CenterY", &name))
    {
        center_point.y = val;
    }
    else if (0 == strcmp("ColorR", &name))
    {
        color.val[R] = val;
    }
    else if (0 == strcmp("ColorG", &name))
    {
        color.val[G] = val;
    }
    else if (0 == strcmp("ColorB", &name))
    {
        color.val[B] = val;
    }
    else if (0 == strcmp("MarkerWidth", &name))
    {
        markerwidth = val;
    }
    else if (0 == strcmp("MarkerHeight", &name))
    {
        markerheight = val;
    }
    else if (0 == strcmp("MarkerShape", &name))
    {
        assert(MarkerCount > val);
        markershape = val;
    }
    else if (0 == strcmp("Tolerance", &name))
    {
        tolerance = val;
    }
    else
    {
        LOG_E("%s/%s: FAILED to act on param %s", __FILE__, __FUNCTION__, &name);
        throw runtime_error("Unknown int parameter.");
    }

    purge_colorarea();
    mtx.unlock();
}

static void param_callback_double(const gchar *name, const gchar *value, void *data)
{
    assert(nullptr != name);
    assert(nullptr != value);
    (void)data;
    if (nullptr == value)
    {
        LOG_E("%s/%s: Unexpected nullptr value for %s", __FILE__, __FUNCTION__, name);
        return;
    }

    LOG_I("Update for parameter %s (%s)", name, value);
    const auto lastdot = strrchr(name, '.');
    assert(nullptr != lastdot);
    assert(1 < strlen(name) - strlen(lastdot));
    update_local_param_double(lastdot[1], atof(value));
}

static void param_callback_int(const gchar *name, const gchar *value, void *data)
{
    assert(nullptr != name);
    assert(nullptr != value);
    (void)data;
    if (nullptr == value)
    {
        LOG_E("%s/%s: Unexpected nullptr value for %s", __FILE__, __FUNCTION__, name);
        return;
    }

    LOG_I("Update for parameter %s (%s)", name, value);
    const auto lastdot = strrchr(name, '.');
    assert(nullptr != lastdot);
    assert(1 < strlen(name) - strlen(lastdot));
    update_local_param_int(lastdot[1], atoi(value));
}

static gboolean setup_param(const gchar *name, AXParameterCallback callbackfn)
{
    assert(nullptr != axparameter);
    GError *error = nullptr;

    assert(nullptr != name);
    assert(nullptr != callbackfn);

    if (!ax_parameter_register_callback(axparameter, name, callbackfn, axparameter, &error))
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

static gboolean setup_param_double(const gchar *name, AXParameterCallback callbackfn)
{
    if (!setup_param(name, callbackfn))
    {
        return FALSE;
    }

    double val;
    if (!get_param_double(name, val))
    {
        LOG_E("%s/%s: Failed to get initial value for %s", __FILE__, __FUNCTION__, name);
        return FALSE;
    }
    update_local_param_double(*name, val);
    LOG_I("%s/%s: Set up parameter %s", __FILE__, __FUNCTION__, name);
    usleep(50000); // mitigate timing issue in parameter handling

    return TRUE;
}

static gboolean setup_param_int(const gchar *name, AXParameterCallback callbackfn)
{
    if (!setup_param(name, callbackfn))
    {
        return FALSE;
    }

    int val;
    if (!get_param_int(name, val))
    {
        LOG_E("%s/%s: Failed to get initial value for %s", __FILE__, __FUNCTION__, name);
        return FALSE;
    }
    update_local_param_int(*name, val);
    LOG_I("%s/%s: Set up parameter %s", __FILE__, __FUNCTION__, name);
    usleep(50000); // mitigate timing issue in parameter handling

    return TRUE;
}

static gboolean imageanalysis(gpointer data)
{
    (void)data;
    // Get the latest NV12 image frame from VDO using the imageprovider
    assert(nullptr != provider);
    VdoBuffer *buf = ImageProvider::GetLastFrameBlocking(*provider);
    if (!buf)
    {
        LOG_I("%s/%s: No more frames available, exiting", __FILE__, __FUNCTION__);
        return TRUE;
    }

    // Assign the VDO image buffer to the nv12_mat OpenCV Mat.
    // This specific Mat is used as it is the one we created for NV12,
    // which has a different layout than e.g., BGR.
    mtx.lock();
    nv12_mat.data = static_cast<uint8_t *>(vdo_buffer_get_data(buf));

    // Convert the NV12 data to BRG
    Mat bgr_mat;
    cvtColor(nv12_mat, bgr_mat, COLOR_YUV2BGR_NV12);

    // Handle request to capture current average color
    if (pickcurrent && nullptr != colorarea)
    {
        color = colorarea->GetAverageColor(bgr_mat);
        LOG_I(
            "%s/%s: Picked current average color: %.1f %.1f %.1f",
            __FILE__,
            __FUNCTION__,
            color.val[R],
            color.val[G],
            color.val[B]);
        if (!set_param_double("ColorB", color.val[B], FALSE) || !set_param_double("ColorG", color.val[G], FALSE) ||
            !set_param_double("ColorR", color.val[R], TRUE))
        {
            LOG_E("%s/%s: Failed to set picked color", __FILE__, __FUNCTION__);
        }
        purge_colorarea();
        pickcurrent = false;
    }

    // Create color area if nonexistent
    if (nullptr == colorarea)
    {
        LOG_I("%s/%s: Set up new colorarea", __FILE__, __FUNCTION__);
        switch (markershape)
        {
        case Ellipse:
            colorarea = new ColorAreaEllipse(bgr_mat, center_point, color, markerwidth, markerheight, tolerance);
            break;
        case Rectangle:
            colorarea = new ColorAreaRectangle(bgr_mat, center_point, color, markerwidth, markerheight, tolerance);
            break;
        default:
            throw runtime_error("Unknown marker shape value used.");
            break;
        }
    }
    assert(nullptr != colorarea);
    const bool newstate = colorarea->ColorAreaValueWithinTolerance(bgr_mat);
    opcuaserver.UpdateColorAreaValue(newstate);
    if (newstate != currentstate)
    {
        // Trigger Axis event for state change
        evhandler.Send(newstate);
        currentstate = newstate;
    }
    mtx.unlock();

    // Release the VDO frame buffer
    ImageProvider::ReturnFrame(*provider, *buf);

    return TRUE;
}

static gboolean initimageanalysis(const unsigned int w, const unsigned int h)
{
    // chooseStreamResolution gets the least resource intensive stream
    // that exceeds or equals the desired resolution specified above
    unsigned int streamWidth = 0;
    unsigned int streamHeight = 0;
    if (!ImageProvider::ChooseStreamResolution(w, h, streamWidth, streamHeight))
    {
        LOG_E("%s/%s: Failed choosing stream resolution", __FILE__, __FUNCTION__);
        return FALSE;
    }

    // Update the ACAP parameters width and height accordingly, for the config
    // UI to read
    GError *error = nullptr;
    char param[128];
    snprintf(param, 127, "%u", streamWidth);
    if (!ax_parameter_set(axparameter, "Width", param, TRUE, &error))
    {
        LOG_E("%s/%s: Failed to update Width", __FILE__, __FUNCTION__);
        g_error_free(error);
        return FALSE;
    }
    snprintf(param, 127, "%u", streamHeight);
    if (!ax_parameter_set(axparameter, "Height", param, TRUE, &error))
    {
        LOG_E("%s/%s: Failed to update Height", __FILE__, __FUNCTION__);
        g_error_free(error);
        return FALSE;
    }

    LOG_I("Creating VDO image provider and creating stream %d x %d", streamWidth, streamHeight);
    provider = new ImageProvider(streamWidth, streamHeight, 2, VDO_FORMAT_YUV);
    if (!provider)
    {
        LOG_E("%s/%s: Failed to create ImageProvider", __FILE__, __FUNCTION__);
        return FALSE;
    }
    if (!provider->InitImageProvider())
    {
        LOG_E("%s/%s: Failed to init ImageProvider", __FILE__, __FUNCTION__);
        return FALSE;
    }

    LOG_I("Start fetching video frames from VDO");
    if (!ImageProvider::StartFrameFetch(*provider))
    {
        LOG_E("%s/%s: Failed to fetch frames from VDO", __FILE__, __FUNCTION__);
        return FALSE;
    }

    // OpenCV represents NV12 with 1.5 bytes per pixel
    nv12_mat = Mat(streamHeight * 3 / 2, streamWidth, CV_8UC1);

    return TRUE;
}

static gboolean pickcurrent_cb()
{
    pickcurrent = true;
    return imageanalysis(nullptr);
}

static gboolean get_color_area_value()
{
    return opcuaserver.GetColorAreaValue();
}

static void signalHandler(int signal_num)
{
    switch (signal_num)
    {
    case SIGTERM:
    case SIGABRT:
    case SIGINT:
        if (nullptr != provider)
        {
            ImageProvider::StopFrameFetch(*provider);
        }
        g_main_loop_quit(loop);
        break;
    default:
        break;
    }
}

static bool initializeSignalHandler(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));

    if (-1 == sigemptyset(&sa.sa_mask))
    {
        LOG_E("Failed to initialize signal handler (%s)", strerror(errno));
        return false;
    }

    sa.sa_handler = signalHandler;

    if (0 > sigaction(SIGTERM, &sa, NULL) || 0 > sigaction(SIGABRT, &sa, NULL) || 0 > sigaction(SIGINT, &sa, NULL))
    {
        LOG_E("Failed to install signal handler (%s)", strerror(errno));
        return false;
    }

    return true;
}

int main(int argc, char *argv[])
{
    (void)argc;

    GError *error = nullptr;
    CgiHandler *cgi_handler = nullptr;
    const auto app_name = basename(argv[0]);
    openlog(app_name, LOG_PID | LOG_CONS, LOG_USER);

    int result = EXIT_SUCCESS;
    if (!initializeSignalHandler())
    {
        result = EXIT_FAILURE;
        goto exit;
    }

    // Init parameter handling (will also launch OPC UA server)
    LOG_I("Init parameter handling ...");
    axparameter = ax_parameter_new(app_name, &error);
    if (nullptr != error)
    {
        LOG_E("%s/%s: ax_parameter_new failed (%s)", __FILE__, __FUNCTION__, error->message);
        g_error_free(error);
        result = EXIT_FAILURE;
        goto exit;
    }
    LOG_I("%s/%s: ax_parameter_new success", __FILE__, __FUNCTION__);
    // clang-format off
    if (!setup_param_int("CenterX", param_callback_int) ||
        !setup_param_int("CenterY", param_callback_int) ||
        !setup_param_double("ColorB", param_callback_double) ||
        !setup_param_double("ColorG", param_callback_double) ||
        !setup_param_double("ColorR", param_callback_double) ||
        !setup_param_int("Height", param_callback_int) ||
        !setup_param_int("MarkerHeight", param_callback_int) ||
        !setup_param_int("MarkerShape", param_callback_int) ||
        !setup_param_int("MarkerWidth", param_callback_int) ||
        !setup_param_int("Port", param_callback_int) ||
        !setup_param_int("Tolerance", param_callback_int) ||
        !setup_param_int("Width", param_callback_int))
    // clang-format on
    {
        LOG_E("%s/%s: Failed to set up parameters", __FILE__, __FUNCTION__);
        result = EXIT_FAILURE;
        goto exit_param;
    }
    // Log retrieved param values
    LOG_I("%s/%s: center: (%u, %u)", __FILE__, __FUNCTION__, center_point.x, center_point.y);
    LOG_I(
        "%s/%s: color (R, G, B) = (%.1f, %.1f, %.1f)",
        __FILE__,
        __FUNCTION__,
        color.val[R],
        color.val[G],
        color.val[B]);
    LOG_I("%s/%s: marker dimenstions (w, h) = (%u, %u)", __FILE__, __FUNCTION__, markerwidth, markerheight);
    LOG_I("%s/%s: marker shape = %u", __FILE__, __FUNCTION__, markershape);
    LOG_I("%s/%s: tolerance: %u", __FILE__, __FUNCTION__, tolerance);

    // Initialize image analysis
    if (!initimageanalysis(640, 360))
    {
        LOG_E("%s/%s: Failed to init image analysis", __FILE__, __FUNCTION__);
        result = EXIT_FAILURE;
        goto exit_param;
    }

    // Add image analysis as idle function
    if (1 > g_idle_add(imageanalysis, nullptr))
    {
        LOG_E("%s/%s: Failed to add idle function", __FILE__, __FUNCTION__);
        result = EXIT_FAILURE;
        goto exit_param;
    }

    // Add means to get value through HTTP too
    cgi_handler = new CgiHandler(get_color_area_value, get_param_double, pickcurrent_cb);
    if (nullptr == cgi_handler)
    {
        LOG_E("%s/%s: Failed to set up CGI handler", __FILE__, __FUNCTION__);
        result = EXIT_FAILURE;
        goto exit_param;
    }

    LOG_I("Start main loop ...");
    assert(nullptr == loop);
    loop = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(loop);

    // Cleanup
    LOG_I("Shutdown ...");
    delete cgi_handler;
    g_main_loop_unref(loop);
    if (nullptr != provider)
    {
        delete provider;
    }
    opcuaserver.ShutDownServer();

exit_param:
    ax_parameter_free(axparameter);

exit:
    LOG_I("Exiting!");
    closelog();

    return result;
}
