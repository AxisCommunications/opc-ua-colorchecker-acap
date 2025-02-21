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
#include <opencv2/imgproc.hpp>
#include <opencv2/video.hpp>
#include <stdexcept>
#include <syslog.h>

#include "CgiHandler.hpp"
#include "ColorArea.hpp"
#include "EventHandler.hpp"
#include "ImageProvider.hpp"
#include "OpcUaServer.hpp"
#include "ParamHandler.hpp"
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

static GMutex mtx;

static EventHandler evhandler;
static ColorArea *colorarea = nullptr;
static OpcUaServer opcuaserver;
static ParamHandler *paramhandler = nullptr;

static ImageProvider *provider = nullptr;
static Mat nv12_mat;

static void purge_colorarea(void)
{
    // Recalibrate color checker at next frame
    if (nullptr != colorarea)
    {
        delete colorarea;
        colorarea = nullptr;
    }
}

static void restart_opcuaserver(const guint32 port)
{
    g_mutex_lock(&mtx);
    if (opcuaserver.IsRunning())
    {
        opcuaserver.ShutDownServer();
    }
    if (!opcuaserver.LaunchServer(port))
    {
        LOG_E("%s/%s: Failed to launch OPC UA server", __FILE__, __FUNCTION__);
        assert(false);
    }
    g_mutex_unlock(&mtx);
}

static gboolean imageanalysis(gpointer data)
{
    (void)data;
    assert(nullptr != paramhandler);
    // Get the latest NV12 image frame from VDO using the imageprovider
    assert(nullptr != provider);
    auto buf = ImageProvider::GetLastFrameBlocking(*provider);
    if (nullptr == buf)
    {
        LOG_I("%s/%s: No more frames available, exiting", __FILE__, __FUNCTION__);
        return TRUE;
    }

    // Assign the VDO image buffer to the nv12_mat OpenCV Mat.
    // This specific Mat is used as it is the one we created for NV12,
    // which has a different layout than e.g., BGR.
    g_mutex_lock(&mtx);
    nv12_mat.data = static_cast<uint8_t *>(vdo_buffer_get_data(buf));

    // Convert the NV12 data to BRG
    Mat bgr_mat;
    cvtColor(nv12_mat, bgr_mat, COLOR_YUV2BGR_NV12);

    // Handle request to capture current average color
    if (pickcurrent && nullptr != colorarea)
    {
        const auto color = colorarea->GetAverageColor(bgr_mat);
        LOG_I(
            "%s/%s: Picked current average color: %.1f %.1f %.1f",
            __FILE__,
            __FUNCTION__,
            color.val[R],
            color.val[G],
            color.val[B]);
        if (!paramhandler->SetColor(color))
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
        switch (paramhandler->GetMarkerShape())
        {
        case Ellipse:
            colorarea = new ColorAreaEllipse(
                bgr_mat,
                paramhandler->GetCenterPoint(),
                paramhandler->GetColor(),
                paramhandler->GetMarkerWidth(),
                paramhandler->GetMarkerHeight(),
                paramhandler->GetTolerance());
            break;
        case Rectangle:
            colorarea = new ColorAreaRectangle(
                bgr_mat,
                paramhandler->GetCenterPoint(),
                paramhandler->GetColor(),
                paramhandler->GetMarkerWidth(),
                paramhandler->GetMarkerHeight(),
                paramhandler->GetTolerance());
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
    g_mutex_unlock(&mtx);

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
    assert(nullptr != paramhandler);
    if (!paramhandler->SetResolution(streamWidth, streamHeight))
    {
        LOG_E("%s/%s: Failed to update resolution", __FILE__, __FUNCTION__);
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

static Scalar get_color()
{
    assert(nullptr != paramhandler);
    return paramhandler->GetColor();
}

static gboolean get_color_area_value()
{
    return opcuaserver.GetColorAreaValue();
}

static gboolean pickcurrent_cb()
{
    pickcurrent = true;
    return imageanalysis(nullptr);
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
    paramhandler = new ParamHandler(app_name, purge_colorarea, restart_opcuaserver);
    if (nullptr == paramhandler)
    {
        LOG_E("%s/%s: Failed to set up parameters", __FILE__, __FUNCTION__);
        result = EXIT_FAILURE;
        goto exit_param;
    }

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
    cgi_handler = new CgiHandler(get_color, get_color_area_value, pickcurrent_cb);
    if (nullptr == cgi_handler)
    {
        LOG_E("%s/%s: Failed to set up CGI handler", __FILE__, __FUNCTION__);
        result = EXIT_FAILURE;
        goto exit_param;
    }

    LOG_I("Create and start main loop ...");
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
    delete paramhandler;

exit:
    LOG_I("Exiting!");
    closelog();

    return result;
}
