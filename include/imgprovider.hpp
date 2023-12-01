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

/**
 * This header file handles the vdo part of the application.
 */

#pragma once

#include <atomic>
#include <pthread.h>
#include <stdbool.h>

#include "vdo-stream.h"
#include "vdo-types.h"

#define _Atomic(X) std::atomic<X>
#define NUM_VDO_BUFFERS (8)

/**
 * brief A type representing a provider of frames from VDO.
 *
 * Keep track of what kind of images the user wants, all the necessary
 * VDO types to setup and maintain a stream, as well as parameters to make
 * the streaming thread safe.
 */
class ImgProvider
{
  public:
    ImgProvider(const unsigned int w, const unsigned int h, const unsigned int numFrames, const VdoFormat format);
    ~ImgProvider();
    bool InitImgProvider();
    static bool ChooseStreamResolution(
        const unsigned int reqWidth,
        const unsigned int reqHeight,
        unsigned int &chosenWidth,
        unsigned int &chosenHeight);
    static bool CreateStream(ImgProvider &provider);
    static bool AllocateVdoBuffers(ImgProvider &provider, VdoStream &vdoStream);
    static void ReleaseVdoBuffers(ImgProvider &provider);
    static VdoBuffer *GetLastFrameBlocking(ImgProvider &provider);
    static void ReturnFrame(ImgProvider &provider, VdoBuffer &buffer);
    static void *threadEntry(void *data);
    static bool StartFrameFetch(ImgProvider &provider);
    static bool StopFrameFetch(ImgProvider &provider);

    /// Keeping track of frames' statuses.
    GQueue *delivered_frames;
    GQueue *processed_frames;

    /// To support fetching frames asynchonously with VDO.
    pthread_mutex_t frame_mutex;
    pthread_cond_t frame_deliver_cond;
    pthread_t fetcher_thread;
    std::atomic_bool shutdown;

  private:
    void RunLoopIteration();
    bool initialized;
    unsigned int width;
    unsigned int height;
    /// Number of frames to keep in the delivered_frames queue.
    unsigned int num_app_frames;
    // Stream configuration parameters.
    VdoFormat vdo_format;
    // Vdo stream and buffers handling.
    VdoStream *vdo_stream;
    VdoBuffer *vdo_buffers[NUM_VDO_BUFFERS];
};
