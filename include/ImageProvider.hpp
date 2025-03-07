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
 * This header file handles the vdo part of the application.
 */

#pragma once

#include <atomic>
#include <pthread.h>
#include <stdbool.h>

#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <vdo-stream.h>
#include <vdo-types.h>
#pragma GCC diagnostic pop

#define _Atomic(X) std::atomic<X>
#define NUM_VDO_BUFFERS (8)

/**
 * brief A type representing a provider of frames from VDO.
 *
 * Keep track of what kind of images the user wants, all the necessary
 * VDO types to setup and maintain a stream, as well as parameters to make
 * the streaming thread safe.
 */
class ImageProvider
{
  public:
    static bool ChooseStreamResolution(
        const unsigned int req_width,
        const unsigned int req_height,
        unsigned int &chosen_width,
        unsigned int &chosen_height);
    static bool StartFrameFetch(ImageProvider &provider);
    static bool StopFrameFetch(ImageProvider &provider);
    static void *threadEntry(void *data);

    ImageProvider(
        const unsigned int width,
        const unsigned int height,
        const unsigned int num_frames,
        const VdoFormat format);
    ~ImageProvider();
    VdoBuffer *GetLastFrameBlocking();
    void ReturnFrame(VdoBuffer &buffer);

  private:
    bool AllocateVdoBuffers();
    void ReleaseVdoBuffers();
    void RunLoopIteration();

    GQueue *delivered_frames_;
    GQueue *processed_frames_;
    pthread_cond_t frame_deliver_cond_;
    pthread_mutex_t frame_mutex_;
    pthread_t fetcher_thread_;
    std::atomic_bool shutdown_;
    unsigned int num_frames_;
    VdoBuffer *vdo_buffers_[NUM_VDO_BUFFERS];
    VdoStream *vdo_stream_;
};
