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
#include <csignal>
#include <memory>
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

static atomic<bool> pickcurrent_(false);
static atomic<bool> currentstate_(false);

static GMainLoop *loop_ = nullptr;
static volatile sig_atomic_t shutdown_requested_ = 0;

static GMutex mtx_;

static EventHandler *evhandler_ = nullptr;
static ColorArea *colorarea_ = nullptr;
static OpcUaServer opcuaserver_;
static ParamHandler *paramhandler_ = nullptr;

static ImageProvider *provider_ = nullptr;
static Mat nv12_mat_;

static void purge_colorarea_with_lock_held(void)
{
    // Recalibrate color checker at next frame
    if (nullptr != colorarea_)
    {
        delete colorarea_;
        colorarea_ = nullptr;
    }
}

static void purge_colorarea(void)
{
    g_mutex_lock(&mtx_);
    purge_colorarea_with_lock_held();
    g_mutex_unlock(&mtx_);
}

static void restart_opcuaserver(const guint32 port)
{
    g_mutex_lock(&mtx_);
    if (opcuaserver_.IsRunning())
    {
        opcuaserver_.ShutDownServer();
    }
    if (!opcuaserver_.LaunchServer(port))
    {
        LOG_E("%s/%s: Failed to launch OPC UA server", __FILE__, __func__);
        assert(false);
    }
    g_mutex_unlock(&mtx_);
}

static gboolean imageanalysis(gpointer data)
{
    (void)data;
    if (shutdown_requested_)
    {
        g_main_loop_quit(loop_);
        return FALSE;
    }
    assert(nullptr != paramhandler_);
    // Get the latest NV12 image frame from VDO using the imageprovider
    assert(nullptr != provider_);
    auto buf = provider_->GetLastFrameBlocking();
    if (nullptr == buf)
    {
        LOG_I("⏳ No more frames available, exiting (%s) ...", __func__);
        return TRUE;
    }

    // Assign the VDO image buffer to the nv12_mat OpenCV Mat.
    // This specific Mat is used as it is the one we created for NV12,
    // which has a different layout than e.g., BGR.
    g_mutex_lock(&mtx_);
    if (shutdown_requested_)
    {
        g_mutex_unlock(&mtx_);
        provider_->ReturnFrame(*buf);
        g_main_loop_quit(loop_);
        return FALSE;
    }
    nv12_mat_.data = static_cast<uint8_t *>(vdo_buffer_get_data(buf));

    // Convert the NV12 data to BRG
    Mat bgr_mat;
    cvtColor(nv12_mat_, bgr_mat, COLOR_YUV2BGR_NV12);

    // Handle request to capture current average color
    if (pickcurrent_ && nullptr != colorarea_)
    {
        const auto color = colorarea_->GetAverageColor(bgr_mat);
        LOG_I(
            "%s/%s: Picked current average color: %.1f %.1f %.1f",
            __FILE__,
            __func__,
            color.val[R],
            color.val[G],
            color.val[B]);
        if (!paramhandler_->SetColor(color))
        {
            LOG_E("%s/%s: Failed to set picked color", __FILE__, __func__);
        }
        purge_colorarea_with_lock_held();
        pickcurrent_ = false;
    }

    // Create color area if nonexistent
    if (nullptr == colorarea_)
    {
        LOG_I("⏳ Setting up new colorarea ...");
        switch (paramhandler_->GetMarkerShape())
        {
        case Ellipse:
            colorarea_ = new ColorAreaEllipse(
                bgr_mat,
                paramhandler_->GetCenterPoint(),
                paramhandler_->GetColor(),
                paramhandler_->GetMarkerWidth(),
                paramhandler_->GetMarkerHeight(),
                paramhandler_->GetTolerance());
            break;
        case Rectangle:
            colorarea_ = new ColorAreaRectangle(
                bgr_mat,
                paramhandler_->GetCenterPoint(),
                paramhandler_->GetColor(),
                paramhandler_->GetMarkerWidth(),
                paramhandler_->GetMarkerHeight(),
                paramhandler_->GetTolerance());
            break;
        default:
            throw runtime_error("Unknown marker shape value used.");
            break;
        }
    }
    assert(nullptr != colorarea_);
    const bool newstate = colorarea_->ColorAreaValueWithinTolerance(bgr_mat);
    opcuaserver_.UpdateColorAreaValue(newstate);
    if (newstate != currentstate_)
    {
        // Trigger Axis event for state change
        assert(nullptr != evhandler_);
        evhandler_->Send(newstate);
        currentstate_ = newstate;
    }
    g_mutex_unlock(&mtx_);

    // Release the VDO frame buffer
    provider_->ReturnFrame(*buf);

    if (shutdown_requested_)
    {
        g_main_loop_quit(loop_);
        return FALSE;
    }

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
        LOG_E("%s/%s: Failed choosing stream resolution", __FILE__, __func__);
        return FALSE;
    }

    // Update the ACAP parameters width and height accordingly, for the config
    // UI to read
    assert(nullptr != paramhandler_);
    if (!paramhandler_->SetResolution(streamWidth, streamHeight))
    {
        LOG_E("%s/%s: Failed to update resolution", __FILE__, __func__);
        return FALSE;
    }

    LOG_I("⏳ Creating VDO image provider and creating stream %d x %d ...", streamWidth, streamHeight);
    provider_ = new ImageProvider(streamWidth, streamHeight, 2, VDO_FORMAT_YUV);
    if (nullptr == provider_)
    {
        LOG_E("%s/%s: Failed to create ImageProvider", __FILE__, __func__);
        return FALSE;
    }

    LOG_I("⏳ Start fetching video frames from VDO ...");
    if (!ImageProvider::StartFrameFetch(*provider_))
    {
        LOG_E("%s/%s: Failed to fetch frames from VDO", __FILE__, __func__);
        return FALSE;
    }

    // OpenCV represents NV12 with 1.5 bytes per pixel
    nv12_mat_ = Mat(streamHeight * 3 / 2, streamWidth, CV_8UC1);

    return TRUE;
}

static Scalar get_color()
{
    assert(nullptr != paramhandler_);
    return paramhandler_->GetColor();
}

static gboolean get_color_area_value()
{
    return opcuaserver_.GetColorAreaValue();
}

static gboolean pickcurrent_cb()
{
    pickcurrent_ = true;
    return imageanalysis(nullptr);
}

static void signalHandler(int signal_num)
{
    LOG_I("🛑 %s", strsignal(signal_num));
    switch (signal_num)
    {
    case SIGTERM:
    case SIGABRT:
    case SIGINT:
        shutdown_requested_ = 1;
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
        LOG_E("%s/%s: Failed to initialize signal handler (%s)", __FILE__, __func__, strerror(errno));
        return false;
    }

    sa.sa_handler = signalHandler;

    if (0 > sigaction(SIGTERM, &sa, NULL) || 0 > sigaction(SIGABRT, &sa, NULL) || 0 > sigaction(SIGINT, &sa, NULL))
    {
        LOG_E("%s/%s: Failed to install signal handler (%s)", __FILE__, __func__, strerror(errno));
        return false;
    }

    return true;
}

int main(int argc, char *argv[])
{
    (void)argc;

    CgiHandler *cgi_handler = nullptr;
    unique_ptr<EventHandler> event_handler;
    const auto app_name = basename(argv[0]);
    openlog(app_name, LOG_PID | LOG_CONS, LOG_USER);

    int result = EXIT_SUCCESS;
    if (!initializeSignalHandler())
    {
        result = EXIT_FAILURE;
        goto exit;
    }

    // Init parameter handling (will also launch OPC UA server)
    event_handler = make_unique<EventHandler>();
    evhandler_ = event_handler.get();
    LOG_I("⏳ Init parameter handling ...");
    paramhandler_ = new ParamHandler(app_name, purge_colorarea, restart_opcuaserver);
    if (nullptr == paramhandler_)
    {
        LOG_E("%s/%s: Failed to set up parameters", __FILE__, __func__);
        result = EXIT_FAILURE;
        goto exit_param;
    }

    // Initialize image analysis
    if (!initimageanalysis(640, 360))
    {
        LOG_E("%s/%s: Failed to init image analysis", __FILE__, __func__);
        result = EXIT_FAILURE;
        goto exit_param;
    }

    // Add image analysis as idle function
    if (1 > g_idle_add(imageanalysis, nullptr))
    {
        LOG_E("%s/%s: Failed to add idle function", __FILE__, __func__);
        result = EXIT_FAILURE;
        goto exit_param;
    }

    // Add means to get value through HTTP too
    cgi_handler = new CgiHandler(get_color, get_color_area_value, pickcurrent_cb);
    if (nullptr == cgi_handler)
    {
        LOG_E("%s/%s: Failed to set up CGI handler", __FILE__, __func__);
        result = EXIT_FAILURE;
        goto exit_param;
    }

    LOG_I("🧹 Create and start main loop ...");
    assert(nullptr == loop_);
    loop_ = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(loop_);

    // Cleanup
    LOG_I("🧹 Shutdown ...");
    delete cgi_handler;
    g_main_loop_unref(loop_);
    if (nullptr != provider_)
    {
        ImageProvider::StopFrameFetch(*provider_);
        delete provider_;
        provider_ = nullptr;
    }
    opcuaserver_.ShutDownServer();

exit_param:
    delete paramhandler_;

exit:
    evhandler_ = nullptr;
    event_handler.reset();
    LOG_I("✅ Exiting!");
    closelog();

    return result;
}
