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

/**
 * This file handles the vdo part of the application.
 */

#include <assert.h>
#include <errno.h>

#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <vdo-channel.h>
#include <vdo-map.h>
#pragma GCC diagnostic pop

#include "ImageProvider.hpp"
#include "common.hpp"

#define VDO_CHANNEL (1)

/**
 * brief Constructor
 *
 * Make sure to check ImageProvider streamWidth and streamHeight members to
 * find resolution of the created stream. These numbers might not match the
 * requested resolution depending on platform properties.
 *
 * param width Requested output image width.
 * param height Requested ouput image height.
 * param numFrames Number of fetched frames to keep.
 * param vdoFormat Image format to be output by stream.
 */
ImageProvider::ImageProvider(
    const unsigned int width,
    const unsigned int height,
    const unsigned int numFrames,
    const VdoFormat format)
    : shutdown_(false), initialized_(false), width_(width), height_(height), num_app_frames_(numFrames),
      vdo_format_(format)
{
}

ImageProvider::~ImageProvider()
{
    ImageProvider::ReleaseVdoBuffers(*this);

    pthread_mutex_destroy(&frame_mutex_);
    pthread_cond_destroy(&frame_deliver_cond_);

    if (delivered_frames_)
    {
        g_queue_free(delivered_frames_);
    }
    if (processed_frames_)
    {
        g_queue_free(processed_frames_);
    }
}

bool ImageProvider::InitImageProvider()
{
    if (pthread_mutex_init(&frame_mutex_, nullptr))
    {
        LOG_E("%s: Unable to initialize mutex: %s", __func__, strerror(errno));
        return false;
    }

    if (pthread_cond_init(&frame_deliver_cond_, nullptr))
    {
        LOG_E("%s: Unable to initialize condition variable: %s", __func__, strerror(errno));
        goto error_mtx;
    }

    delivered_frames_ = g_queue_new();
    if (!delivered_frames_)
    {
        LOG_E("%s: Unable to create delivered_frames queue!", __func__);
        goto error_cond;
    }

    processed_frames_ = g_queue_new();
    if (!processed_frames_)
    {
        LOG_E("%s: Unable to create processed_frames queue!", __func__);
        goto error_cond;
    }

    if (!CreateStream(*this))
    {
        LOG_E("%s: Could not create VDO stream!", __func__);
        goto error_cond;
    }

    initialized_ = true;
    goto exit;

error_cond:
    pthread_cond_destroy(&frame_deliver_cond_);
error_mtx:
    pthread_mutex_destroy(&frame_mutex_);
exit:
    return initialized_;
}

/**
 * brief Find VDO resolution that best fits requirement.
 *
 * Queries available stream resolutions from VDO and selects the smallest that
 * fits the requested width and height. If no valid resolutions are reported
 * by VDO then the original w/h are returned as chosenWidth/chosenHeight.
 *
 * param reqWidth Requested image width.
 * param reqHeight Requested image height.
 * param chosenWidth Selected image width.
 * param chosenHeight Selected image height.
 * return False if any errors occur, otherwise true.
 */
bool ImageProvider::ChooseStreamResolution(
    const unsigned int reqWidth,
    const unsigned int reqHeight,
    unsigned int &chosenWidth,
    unsigned int &chosenHeight)
{
    GError *error = nullptr;

    // Retrieve channel resolutions
    auto channel = vdo_channel_get(VDO_CHANNEL, &error);
    if (nullptr == channel)
    {
        LOG_E("%s: Failed vdo_channel_get(): %s", __func__, (error != nullptr) ? error->message : "N/A");
        g_clear_object(&channel);
        g_clear_error(&error);
    }
    auto set = vdo_channel_get_resolutions(channel, nullptr, &error);
    g_clear_object(&channel);
    if (nullptr == set)
    {
        LOG_E(
            "%s/%s: vdo_channel_get_resolutions() failed (%s)",
            __FILE__,
            __FUNCTION__,
            (error != nullptr) ? error->message : "N/A");
        g_clear_error(&error);
        return false;
    }

    // Find smallest VDO stream resolution that fits the requested size.
    ssize_t bestResolutionIdx = -1;
    unsigned int bestResolutionArea = UINT_MAX;
    for (ssize_t i = 0; (gsize)i < set->count; ++i)
    {
        VdoResolution *res = &set->resolutions[i];
        assert(nullptr != res);
        LOG_I("%s/%s: resolution %zu: (%ux%u)", __FILE__, __FUNCTION__, i, res->width, res->height);
        if ((res->width >= reqWidth) && (res->height >= reqHeight))
        {
            unsigned int area = res->width * res->height;
            if (area < bestResolutionArea)
            {
                bestResolutionIdx = i;
                bestResolutionArea = area;
            }
        }
    }

    // If we got a reasonable w/h from the VDO channel info we use that
    // for creating the stream. If that info for some reason was empty we
    // fall back to trying to create a stream with client-supplied w/h.
    chosenWidth = reqWidth;
    chosenWidth = reqHeight;
    if (bestResolutionIdx >= 0)
    {
        chosenWidth = set->resolutions[bestResolutionIdx].width;
        chosenHeight = set->resolutions[bestResolutionIdx].height;
        LOG_I(
            "%s/%s: We select stream %ux%u based on VDO channel info",
            __FILE__,
            __FUNCTION__,
            chosenWidth,
            chosenHeight);
    }
    else
    {
        LOG_E(
            "%s/%s: VDO channel info contains no resolution info. Fallback to client-requested stream resolution "
            "%ux%u.",
            __FILE__,
            __FUNCTION__,
            chosenWidth,
            chosenHeight);
    }

    g_free(set);

    return true;
}

/**
 * brief Set up a stream through VDO.
 *
 * Set up stream settings, allocate image buffers and map memory.
 *
 * param provider ImageProvider reference.
 * return False if any errors occur, otherwise true.
 */
bool ImageProvider::CreateStream(ImageProvider &provider)
{
    assert(!provider.initialized_);
    auto vdoMap = vdo_map_new();
    GError *error = nullptr;
    auto ret = false;

    if (nullptr == vdoMap)
    {
        LOG_E("%s: Failed to create vdo_map", __func__);
        return ret;
    }

    vdo_map_set_uint32(vdoMap, "channel", VDO_CHANNEL);
    vdo_map_set_uint32(vdoMap, "format", provider.vdo_format_);
    vdo_map_set_uint32(vdoMap, "width", provider.width_);
    vdo_map_set_uint32(vdoMap, "height", provider.height_);
    // We will use buffer_alloc() and buffer_unref() calls.
    vdo_map_set_uint32(vdoMap, "buffer.strategy", VDO_BUFFER_STRATEGY_EXPLICIT);

    LOG_I("Dump of vdo stream settings map =====");
    vdo_map_dump(vdoMap);

    auto vdo_stream_ = vdo_stream_new(vdoMap, nullptr, &error);
    if (nullptr == vdo_stream_)
    {
        LOG_E("%s: Failed creating VDO stream (%s)", __func__, (error != nullptr) ? error->message : "N/A");
        goto create_exit;
    }

    if (!ImageProvider::AllocateVdoBuffers(provider, *vdo_stream_))
    {
        LOG_E("%s: Failed setting up VDO buffers!", __func__);
        ImageProvider::ReleaseVdoBuffers(provider);
        goto create_exit;
    }

    // Start the actual VDO streaming.
    if (!vdo_stream_start(vdo_stream_, &error))
    {
        LOG_E("%s: Failed starting stream: %s", __func__, (error != nullptr) ? error->message : "N/A");
        ImageProvider::ReleaseVdoBuffers(provider);
        goto create_exit;
    }

    provider.vdo_stream_ = vdo_stream_;

    ret = true;

create_exit:
    g_object_unref(vdoMap);
    g_clear_error(&error);
    return ret;
}
/**
 * brief Allocate VDO buffers on a stream.
 *
 * Note that buffers are not relased upon error condition.
 *
 * param provider ImageProvider reference.
 * param vdoStream VDO stream for buffer allocation.
 * return False if any errors occur, otherwise true.
 */
bool ImageProvider::AllocateVdoBuffers(ImageProvider &provider, VdoStream &vdoStream)
{
    assert(!provider.initialized_);
    GError *error = nullptr;
    auto ret = false;

    for (size_t i = 0; i < NUM_VDO_BUFFERS; i++)
    {
        provider.vdo_buffers_[i] = vdo_stream_buffer_alloc(&vdoStream, nullptr, &error);
        if (provider.vdo_buffers_[i] == nullptr)
        {
            LOG_E(
                "%s/%s: Failed creating VDO buffer: %s",
                __FILE__,
                __FUNCTION__,
                (error != nullptr) ? error->message : "N/A");
            goto error_exit;
        }

        // Make a 'speculative' vdo_buffer_get_data() call to trigger a
        // memory mapping of the buffer. The mapping is cached in the VDO
        // implementation.
        auto dummyPtr = vdo_buffer_get_data(provider.vdo_buffers_[i]);
        if (nullptr == dummyPtr)
        {
            LOG_E(
                "%s/%s: Failed initializing buffer memmap: %s",
                __FILE__,
                __FUNCTION__,
                (error != nullptr) ? error->message : "N/A");
            goto error_exit;
        }

        if (!vdo_stream_buffer_enqueue(&vdoStream, provider.vdo_buffers_[i], &error))
        {
            LOG_E("%s: Failed enqueue VDO buffer: %s", __func__, (error != nullptr) ? error->message : "N/A");
            goto error_exit;
        }
    }

    ret = true;

error_exit:
    g_clear_error(&error);

    return ret;
}

/**
 * brief Release references to the buffers we allocated in CreateStream().
 *
 * param provider Reference to ImageProvider owning the buffer references.
 */
void ImageProvider::ReleaseVdoBuffers(ImageProvider &provider)
{
    if (nullptr == provider.vdo_stream_)
    {
        return;
    }

    for (size_t i = 0; i < NUM_VDO_BUFFERS; i++)
    {
        if (nullptr != provider.vdo_buffers_[i])
        {
            vdo_stream_buffer_unref(provider.vdo_stream_, &provider.vdo_buffers_[i], nullptr);
        }
    }
}

/**
 * brief Get the most recent frame the thread has fetched from VDO.
 *
 * param provider Reference to an ImageProvider fetching frames.
 * return Pointer to an image buffer on success, otherwise nullptr.
 */
VdoBuffer *ImageProvider::GetLastFrameBlocking(ImageProvider &provider)
{
    assert(provider.initialized_);
    VdoBuffer *returnBuf = nullptr;
    pthread_mutex_lock(&provider.frame_mutex_);

    while (g_queue_get_length(provider.delivered_frames_) < 1)
    {
        if (pthread_cond_wait(&provider.frame_deliver_cond_, &provider.frame_mutex_))
        {
            LOG_E("%s: Failed to wait on condition: %s", __func__, strerror(errno));
            goto error_exit;
        }
    }

    returnBuf = (VdoBuffer *)g_queue_pop_tail(provider.delivered_frames_);

error_exit:
    pthread_mutex_unlock(&provider.frame_mutex_);

    return returnBuf;
}

void ImageProvider::ReturnFrame(ImageProvider &provider, VdoBuffer &buffer)
{
    assert(provider.initialized_);
    pthread_mutex_lock(&provider.frame_mutex_);

    g_queue_push_tail(provider.processed_frames_, &buffer);

    pthread_mutex_unlock(&provider.frame_mutex_);
}

void ImageProvider::RunLoopIteration()
{
    assert(initialized_);
    GError *error = nullptr;
    // Block waiting for a frame from VDO
    auto newBuffer = vdo_stream_get_buffer(vdo_stream_, &error);

    if (nullptr == newBuffer)
    {
        // Fail but we continue anyway hoping for the best.
        LOG_I("%s: WARNING, failed fetching frame from vdo: %s", __func__, (error != nullptr) ? error->message : "N/A");
        g_clear_error(&error);
        return;
    }
    pthread_mutex_lock(&frame_mutex_);

    g_queue_push_tail(delivered_frames_, newBuffer);

    VdoBuffer *oldBuffer = nullptr;

    // First check if there are any frames returned from app
    // processing
    if (g_queue_get_length(processed_frames_) > 0)
    {
        oldBuffer = (VdoBuffer *)g_queue_pop_head(processed_frames_);
    }
    else
    {
        // Client specifies the number-of-recent-frames it needs to collect
        // in one chunk (numAppFrames). Thus only enqueue buffers back to
        // VDO if we have collected more buffers than numAppFrames.
        if (g_queue_get_length(delivered_frames_) > num_app_frames_)
        {
            oldBuffer = (VdoBuffer *)g_queue_pop_head(delivered_frames_);
        }
    }

    if (oldBuffer)
    {
        if (!vdo_stream_buffer_enqueue(vdo_stream_, oldBuffer, &error))
        {
            // Fail but we continue anyway hoping for the best.
            syslog(
                LOG_WARNING,
                "%s: Failed enqueueing buffer to vdo: %s",
                __func__,
                (error != nullptr) ? error->message : "N/A");
            g_clear_error(&error);
        }
    }
    g_object_unref(newBuffer); // Release the ref from vdo_stream_get_buffer
    pthread_cond_signal(&frame_deliver_cond_);
    pthread_mutex_unlock(&frame_mutex_);
}

/**
 * brief Starting point function for the thread fetching frames.
 *
 * Responsible for fetching buffers/frames from VDO and re-enqueue buffers back
 * to VDO when they are not needed by the application. The ImageProvider always
 * keeps one or several of the most recent frames available in the application.
 * There are two queues involved: delivered_frames and processed_frames.
 * - delivered_frames are frames delivered from VDO and
 *   not processed by the client.
 * - processed_frames are frames that the client has consumed and handed
 *   back to the ImageProvider.
 * The thread works roughly like this:
 * 1. The thread blocks on vdo_stream_get_buffer() until VDO deliver a new
 * frame.
 * 2. The fresh frame is put at the end of the deliveredFrame queue. If the
 *    client want to fetch a frame the item at the end of deliveredFrame
 *    list is returned.
 * 3. If there are any frames in the processed_frames list one of these are
 *    enqueued back to VDO to keep the flow of buffers.
 * 4. If the processed_frames list is empty we instead check if there are
 *    frames available in the delivered_frames list. We want to make sure
 *    there is at least numAppFrames buffers available to the client to
 *    fetch. If there are more than numAppFrames in delivered_frames we
 *    pick the first buffer (oldest) in the list and enqueue it to VDO.

 * param data Pointer to ImageProvider owning thread.
 * return Pointer to unused return data.
 */
void *ImageProvider::threadEntry(void *data)
{
    assert(nullptr != data);
    auto provider = static_cast<ImageProvider *>(data);
    assert(provider->initialized_);

    while (!provider->shutdown_)
    {
        provider->RunLoopIteration();
    }
    return nullptr;
}

bool ImageProvider::StartFrameFetch(ImageProvider &provider)
{
    assert(provider.initialized_);
    if (pthread_create(&provider.fetcher_thread_, nullptr, provider.threadEntry, &provider))
    {
        LOG_E("%s: Failed to start thread fetching frames from vdo: %s", __func__, strerror(errno));
        return false;
    }

    return true;
}

bool ImageProvider::StopFrameFetch(ImageProvider &provider)
{
    provider.shutdown_ = true;

    if (pthread_join(provider.fetcher_thread_, nullptr))
    {
        LOG_E("%s: Failed to join thread fetching frames from vdo: %s", __func__, strerror(errno));
        return false;
    }

    return true;
}
