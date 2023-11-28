/**
 * Copyright (C) 2023, Axis Communications AB, Lund, Sweden
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
#include <axhttp.h>
#include <axparameter.h>
#include <mutex>
#include <opencv2/imgproc.hpp>
#include <opencv2/video.hpp>
#include <stdexcept>
#include <string>
#include <syslog.h>

#include "colorarea.hpp"
#include "common.hpp"
#include "evhandler.hpp"
#include "imgprovider.hpp"
#include "opcuaserver.hpp"

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

static AxEventHandler evhandler;
static AXParameter *axparameter = nullptr;
static Point center_point;
static Scalar color;
static uint32_t markerwidth;
static uint32_t markerheight;
static uint8_t markershape;
static uint8_t tolerance;
static ColorArea *colorarea = nullptr;
static OpcUaServer opcuaserver;

static ImgProvider *provider = nullptr;
static Mat nv12_mat;

static gboolean set_param(AXParameter &axparameter, const gchar *name, const gchar &value, gboolean do_sync = TRUE)
{
    GError *error = nullptr;

    if (!ax_parameter_set(&axparameter, name, &value, do_sync, &error))
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

static gboolean
set_param_double(AXParameter &axparameter, const gchar *name, const double value, gboolean do_sync = TRUE)
{
    gchar *valuestr = g_strdup_printf("%f", value);
    assert(nullptr != valuestr);
    gboolean result = set_param(axparameter, name, *valuestr, do_sync);
    g_free(valuestr);
    return result;
}

static gchar *get_param(AXParameter &axparameter, const gchar *name)
{
    GError *error = nullptr;
    gchar *value = nullptr;
    if (!ax_parameter_get(&axparameter, name, &value, &error))
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

static gboolean get_param_double(AXParameter &axparameter, const gchar *name, double &val)
{
    auto valuestr = get_param(axparameter, name);
    if (nullptr == valuestr)
    {
        return FALSE;
    }
    val = atof(valuestr);
    g_free(valuestr);
    return TRUE;
}

static gboolean get_param_int(AXParameter &axparameter, const gchar *name, int &val)
{
    auto valuestr = get_param(axparameter, name);
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
    auto lastdot = strrchr(name, '.');
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
    auto lastdot = strrchr(name, '.');
    assert(nullptr != lastdot);
    assert(1 < strlen(name) - strlen(lastdot));
    update_local_param_int(lastdot[1], atoi(value));
}

static gboolean setup_param(AXParameter &axparameter, const gchar *name, AXParameterCallback callbackfn)
{
    GError *error = nullptr;

    assert(nullptr != name);
    assert(nullptr != callbackfn);

    if (!ax_parameter_register_callback(&axparameter, name, callbackfn, &axparameter, &error))
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

static gboolean setup_param_double(AXParameter &axparameter, const gchar *name, AXParameterCallback callbackfn)
{
    if (!setup_param(axparameter, name, callbackfn))
    {
        return FALSE;
    }

    double val;
    if (!get_param_double(axparameter, name, val))
    {
        LOG_E("%s/%s: Failed to get initial value for %s", __FILE__, __FUNCTION__, name);
        return FALSE;
    }
    update_local_param_double(*name, val);
    LOG_I("%s/%s: Set up parameter %s", __FILE__, __FUNCTION__, name);
    usleep(50000); // mitigate timing issue in parameter handling

    return TRUE;
}

static gboolean setup_param_int(AXParameter &axparameter, const gchar *name, AXParameterCallback callbackfn)
{
    if (!setup_param(axparameter, name, callbackfn))
    {
        return FALSE;
    }

    int val;
    if (!get_param_int(axparameter, name, val))
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
    VdoBuffer *buf = ImgProvider::GetLastFrameBlocking(*provider);
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
        assert(nullptr != axparameter);
        if (!set_param_double(*axparameter, "ColorB", color.val[B], FALSE) ||
            !set_param_double(*axparameter, "ColorG", color.val[G], FALSE) ||
            !set_param_double(*axparameter, "ColorR", color.val[R], TRUE))
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
    ImgProvider::ReturnFrame(*provider, *buf);

    return TRUE;
}

static gboolean initimageanalysis(AXParameter &axparameter, const unsigned int w, const unsigned int h)
{
    // chooseStreamResolution gets the least resource intensive stream
    // that exceeds or equals the desired resolution specified above
    unsigned int streamWidth = 0;
    unsigned int streamHeight = 0;
    if (!ImgProvider::ChooseStreamResolution(w, h, streamWidth, streamHeight))
    {
        LOG_E("%s/%s: Failed choosing stream resolution", __FILE__, __FUNCTION__);
        return FALSE;
    }

    // Update the ACAP parameters width and height accordingly, for the config
    // UI to read
    GError *error = nullptr;
    char param[128];
    snprintf(param, 127, "%u", streamWidth);
    if (!ax_parameter_set(&axparameter, "Width", param, TRUE, &error))
    {
        LOG_E("%s/%s: Failed to update Width", __FILE__, __FUNCTION__);
        g_error_free(error);
        return FALSE;
    }
    snprintf(param, 127, "%u", streamHeight);
    if (!ax_parameter_set(&axparameter, "Height", param, TRUE, &error))
    {
        LOG_E("%s/%s: Failed to update Height", __FILE__, __FUNCTION__);
        g_error_free(error);
        return FALSE;
    }

    LOG_I("Creating VDO image provider and creating stream %d x %d", streamWidth, streamHeight);
    provider = new ImgProvider(streamWidth, streamHeight, 2, VDO_FORMAT_YUV);
    if (!provider)
    {
        LOG_E("%s/%s: Failed to create ImgProvider", __FILE__, __FUNCTION__);
        return FALSE;
    }
    if (!provider->InitImgProvider())
    {
        LOG_E("%s/%s: Failed to init ImgProvider", __FILE__, __FUNCTION__);
        return FALSE;
    }

    LOG_I("Start fetching video frames from VDO");
    if (!ImgProvider::StartFrameFetch(*provider))
    {
        LOG_E("%s/%s: Failed to fetch frames from VDO", __FILE__, __FUNCTION__);
        return FALSE;
    }

    // OpenCV represents NV12 with 1.5 bytes per pixel
    nv12_mat = Mat(streamHeight * 3 / 2, streamWidth, CV_8UC1);

    return TRUE;
}

static void write_error_response(GDataOutputStream &dos, const int statuscode, const char *statusname, const char *msg)
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

static void write_bad_request(GDataOutputStream &dos, const char *msg)
{
    write_error_response(dos, 400, "Bad Request", msg);
}

static void write_internal_error(GDataOutputStream &dos, const char *msg)
{
    write_error_response(dos, 500, "Internal Server Error", msg);
}

static void request_handler(
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
    (void)user_data;

    auto dos = g_data_output_stream_new(output_stream);
    assert(nullptr != dos);

    const char *func = basename(const_cast<char *>(path));
    if (0 == strcmp("getstatus.cgi", func))
    {
        const bool status = opcuaserver.GetColorAreaValue();
        g_data_output_stream_put_string(dos, "Status: 200 OK\r\n", nullptr, nullptr);
        g_data_output_stream_put_string(dos, "Content-Type: application/json\r\n\r\n", nullptr, nullptr);
        ostringstream ss;
        ss << "{\"status\": " << (status ? "true" : "false") << "}" << endl;
        g_data_output_stream_put_string(dos, ss.str().c_str(), nullptr, nullptr);
    }
    else if (0 == strcmp("pickcurrent.cgi", func))
    {
        pickcurrent = true;
        if (!imageanalysis(nullptr))
        {
            write_internal_error(*dos, "Failed to pick current color");
            goto http_exit;
        }
        double blue;
        double green;
        double red;
        assert(nullptr != axparameter);
        if (!get_param_double(*axparameter, "ColorB", blue) || !get_param_double(*axparameter, "ColorG", green) ||
            !get_param_double(*axparameter, "ColorR", red))
        {
            write_internal_error(*dos, "Inner parameter retrieval error");
            goto http_exit;
        }
        g_data_output_stream_put_string(dos, "Status: 200 OK\r\n", nullptr, nullptr);
        g_data_output_stream_put_string(dos, "Content-Type: application/json\r\n\r\n", nullptr, nullptr);
        ostringstream ss;
        ss << "{\"R\":" << red << ", \"G\":" << green << ", \"B\":" << blue << "}" << endl;
        g_data_output_stream_put_string(dos, ss.str().c_str(), nullptr, nullptr);
    }
    else
    {
        write_bad_request(*dos, "Unknown action");
    }
http_exit:
    g_object_unref(dos);
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
            ImgProvider::StopFrameFetch(*provider);
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
    (void)argv;

    AXHttpHandler *axhttp = nullptr;
    GError *error = nullptr;
    const char *app_name = "opcuacolorchecker";
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
    if (!setup_param_int(*axparameter, "CenterX", param_callback_int) ||
        !setup_param_int(*axparameter, "CenterY", param_callback_int) ||
        !setup_param_double(*axparameter, "ColorB", param_callback_double) ||
        !setup_param_double(*axparameter, "ColorG", param_callback_double) ||
        !setup_param_double(*axparameter, "ColorR", param_callback_double) ||
        !setup_param_int(*axparameter, "Height", param_callback_int) ||
        !setup_param_int(*axparameter, "MarkerHeight", param_callback_int) ||
        !setup_param_int(*axparameter, "MarkerShape", param_callback_int) ||
        !setup_param_int(*axparameter, "MarkerWidth", param_callback_int) ||
        !setup_param_int(*axparameter, "Port", param_callback_int) ||
        !setup_param_int(*axparameter, "Tolerance", param_callback_int) ||
        !setup_param_int(*axparameter, "Width", param_callback_int))
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
    if (!initimageanalysis(*axparameter, 640, 480))
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
    axhttp = ax_http_handler_new(request_handler, &pickcurrent);
    if (nullptr == axhttp)
    {
        LOG_E("%s/%s: Failed to set up HTTP handler", __FILE__, __FUNCTION__);
        result = EXIT_FAILURE;
        goto exit_param;
    }

    LOG_I("Start main loop ...");
    assert(nullptr == loop);
    loop = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(loop);

    // Cleanup
    LOG_I("Shutdown ...");
    ax_http_handler_free(axhttp);
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
