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
 * brief Find VDO resolution that best fits requirement.
 *
 * Queries available stream resolutions from VDO and selects the smallest that
 * fits the requested width and height. If no valid resolutions are reported
 * by VDO then the original w/h are returned as chosen_width/chosen_height.
 *
 * param req_width Requested image width.
 * param req_height Requested image height.
 * param chosen_width Selected image width.
 * param chosen_height Selected image height.
 * return False if any errors occur, otherwise true.
 */
bool ImageProvider::ChooseStreamResolution(
    const unsigned int req_width,
    const unsigned int req_height,
    unsigned int &chosen_width,
    unsigned int &chosen_height)
{
    g_autoptr(GError) error = nullptr;

    // Retrieve channel resolutions
    auto channel = vdo_channel_get(VDO_CHANNEL, &error);
    if (nullptr == channel)
    {
        LOG_E("%s: Failed vdo_channel_get(): %s", __func__, (error != nullptr) ? error->message : "N/A");
        g_clear_object(&channel);
    }
    const auto set = vdo_channel_get_resolutions(channel, nullptr, &error);
    g_clear_object(&channel);
    if (nullptr == set)
    {
        LOG_E(
            "%s/%s: vdo_channel_get_resolutions() failed (%s)",
            __FILE__,
            __FUNCTION__,
            (error != nullptr) ? error->message : "N/A");
        return false;
    }

    // Find smallest VDO stream resolution that fits the requested size.
    ssize_t best_res_idx = -1;
    auto best_res_area = UINT_MAX;
    for (gsize i = 0; set->count > i; ++i)
    {
        const auto res = &set->resolutions[i];
        assert(nullptr != res);
        LOG_I("%s/%s: resolution %zu: (%ux%u)", __FILE__, __FUNCTION__, i, res->width, res->height);
        if ((res->width >= req_width) && (res->height >= req_height))
        {
            const auto area = res->width * res->height;
            if (area < best_res_area)
            {
                best_res_idx = i;
                best_res_area = area;
            }
        }
    }

    // If we got a reasonable w/h from the VDO channel info we use that
    // for creating the stream. If that info for some reason was empty we
    // fall back to trying to create a stream with client-supplied w/h.
    chosen_width = req_width;
    chosen_width = req_height;
    if (0 <= best_res_idx)
    {
        chosen_width = set->resolutions[best_res_idx].width;
        chosen_height = set->resolutions[best_res_idx].height;
        LOG_I(
            "%s/%s: We select stream %ux%u based on VDO channel info",
            __FILE__,
            __FUNCTION__,
            chosen_width,
            chosen_height);
    }
    else
    {
        LOG_E(
            "%s/%s: VDO channel info contains no resolution info. Fallback to client-requested stream resolution "
            "%ux%u.",
            __FILE__,
            __FUNCTION__,
            chosen_width,
            chosen_height);
    }

    g_free(set);

    return true;
}

bool ImageProvider::StartFrameFetch(ImageProvider &provider)
{
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

    while (!provider->shutdown_)
    {
        provider->RunLoopIteration();
    }
    return nullptr;
}

/**
 * brief Constructor
 *
 * Make sure to check ImageProvider streamWidth and streamHeight members to
 * find resolution of the created stream. These numbers might not match the
 * requested resolution depending on platform properties.
 *
 * param width Requested output image width.
 * param height Requested ouput image height.
 * param num_frames Number of fetched frames to keep.
 * param format Image format to be output by stream.
 */
ImageProvider::ImageProvider(
    const unsigned int width,
    const unsigned int height,
    const unsigned int num_frames,
    const VdoFormat format)
    : delivered_frames_(g_queue_new()), processed_frames_(g_queue_new()), shutdown_(false), num_frames_(num_frames)
{
    assert(nullptr != delivered_frames_);
    assert(nullptr != processed_frames_);

    if (pthread_mutex_init(&frame_mutex_, nullptr))
    {
        LOG_E("%s: Unable to initialize mutex: %s", __func__, strerror(errno));
        assert(false);
    }

    if (pthread_cond_init(&frame_deliver_cond_, nullptr))
    {
        LOG_E("%s: Unable to initialize condition variable: %s", __func__, strerror(errno));
        pthread_mutex_destroy(&frame_mutex_);
        assert(false);
    }

    const auto vdoMap = vdo_map_new();
    assert(nullptr != vdoMap);
    g_autoptr(GError) error = nullptr;

    vdo_map_set_uint32(vdoMap, "channel", VDO_CHANNEL);
    vdo_map_set_uint32(vdoMap, "format", format);
    vdo_map_set_uint32(vdoMap, "width", width);
    vdo_map_set_uint32(vdoMap, "height", height);
    // We will use buffer_alloc() and buffer_unref() calls.
    vdo_map_set_uint32(vdoMap, "buffer.strategy", VDO_BUFFER_STRATEGY_EXPLICIT);

    LOG_I("Dump of vdo stream settings map =====");
    vdo_map_dump(vdoMap);

    vdo_stream_ = vdo_stream_new(vdoMap, nullptr, &error);
    g_object_unref(vdoMap);
    if (nullptr == vdo_stream_)
    {
        LOG_E("%s: Failed creating VDO stream (%s)", __func__, (error != nullptr) ? error->message : "N/A");
        assert(false);
    }

    if (!AllocateVdoBuffers())
    {
        LOG_E("%s: Failed setting up VDO buffers!", __func__);
        ReleaseVdoBuffers();
        assert(false);
    }

    // Start the actual VDO streaming.
    if (!vdo_stream_start(vdo_stream_, &error))
    {
        LOG_E("%s: Failed starting stream: %s", __func__, (error != nullptr) ? error->message : "N/A");
        ReleaseVdoBuffers();
        assert(false);
    }
}

ImageProvider::~ImageProvider()
{
    ReleaseVdoBuffers();

    pthread_mutex_destroy(&frame_mutex_);
    pthread_cond_destroy(&frame_deliver_cond_);

    if (nullptr != delivered_frames_)
    {
        g_queue_free(delivered_frames_);
    }
    if (nullptr != processed_frames_)
    {
        g_queue_free(processed_frames_);
    }
}

/**
 * brief Get the most recent frame the thread has fetched from VDO.
 *
 * return Pointer to an image buffer on success, otherwise nullptr.
 */
VdoBuffer *ImageProvider::GetLastFrameBlocking()
{
    VdoBuffer *returnBuf = nullptr;
    pthread_mutex_lock(&frame_mutex_);

    while (1 > g_queue_get_length(delivered_frames_))
    {
        if (pthread_cond_wait(&frame_deliver_cond_, &frame_mutex_))
        {
            LOG_E("%s: Failed to wait on condition: %s", __func__, strerror(errno));
            goto error_exit;
        }
    }

    returnBuf = (VdoBuffer *)g_queue_pop_tail(delivered_frames_);

error_exit:
    pthread_mutex_unlock(&frame_mutex_);

    return returnBuf;
}

void ImageProvider::ReturnFrame(VdoBuffer &buffer)
{
    pthread_mutex_lock(&frame_mutex_);

    g_queue_push_tail(processed_frames_, &buffer);

    pthread_mutex_unlock(&frame_mutex_);
}

bool ImageProvider::AllocateVdoBuffers()
{
    g_autoptr(GError) error = nullptr;

    for (size_t i = 0; NUM_VDO_BUFFERS > i; i++)
    {
        vdo_buffers_[i] = vdo_stream_buffer_alloc(vdo_stream_, nullptr, &error);
        if (vdo_buffers_[i] == nullptr)
        {
            LOG_E(
                "%s/%s: Failed creating VDO buffer: %s",
                __FILE__,
                __FUNCTION__,
                (error != nullptr) ? error->message : "N/A");
            return false;
        }

        // Make a 'speculative' vdo_buffer_get_data() call to trigger a
        // memory mapping of the buffer. The mapping is cached in the VDO
        // implementation.
        const auto dummy_ptr = vdo_buffer_get_data(vdo_buffers_[i]);
        if (nullptr == dummy_ptr)
        {
            LOG_E(
                "%s/%s: Failed initializing buffer memmap: %s",
                __FILE__,
                __FUNCTION__,
                (error != nullptr) ? error->message : "N/A");
            return false;
        }

        if (!vdo_stream_buffer_enqueue(vdo_stream_, vdo_buffers_[i], &error))
        {
            LOG_E("%s: Failed enqueue VDO buffer: %s", __func__, (error != nullptr) ? error->message : "N/A");
            return false;
        }
    }

    return true;
}

void ImageProvider::ReleaseVdoBuffers()
{
    if (nullptr == vdo_stream_)
    {
        return;
    }

    for (auto i = 0; i < NUM_VDO_BUFFERS; i++)
    {
        if (nullptr != vdo_buffers_[i])
        {
            vdo_stream_buffer_unref(vdo_stream_, &vdo_buffers_[i], nullptr);
        }
    }
}

void ImageProvider::RunLoopIteration()
{
    g_autoptr(GError) error = nullptr;
    // Block waiting for a frame from VDO
    const auto new_buffer = vdo_stream_get_buffer(vdo_stream_, &error);

    if (nullptr == new_buffer)
    {
        // Fail but we continue anyway hoping for the best.
        LOG_I("%s: WARNING, failed fetching frame from vdo: %s", __func__, (error != nullptr) ? error->message : "N/A");
        return;
    }
    pthread_mutex_lock(&frame_mutex_);

    g_queue_push_tail(delivered_frames_, new_buffer);

    VdoBuffer *old_buffer = nullptr;

    // First check if there are any frames returned from app
    // processing
    if (g_queue_get_length(processed_frames_) > 0)
    {
        old_buffer = static_cast<VdoBuffer *>(g_queue_pop_head(processed_frames_));
    }
    else
    {
        // Client specifies the number-of-recent-frames it needs to collect
        // in one chunk (num_frames_). Thus only enqueue buffers back to
        // VDO if we have collected more buffers than num_frames_.
        if (g_queue_get_length(delivered_frames_) > num_frames_)
        {
            old_buffer = static_cast<VdoBuffer *>(g_queue_pop_head(delivered_frames_));
        }
    }

    if (old_buffer)
    {
        if (!vdo_stream_buffer_enqueue(vdo_stream_, old_buffer, &error))
        {
            // Fail but we continue anyway hoping for the best.
            LOG_I(
                "%s: WARNING, failed enqueueing buffer to vdo: %s",
                __func__,
                (error != nullptr) ? error->message : "N/A");
        }
    }
    g_object_unref(new_buffer); // Release the ref from vdo_stream_get_buffer
    pthread_cond_signal(&frame_deliver_cond_);
    pthread_mutex_unlock(&frame_mutex_);
}