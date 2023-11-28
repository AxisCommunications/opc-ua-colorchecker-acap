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

#pragma once

#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

enum ColorComponent
{
    B = 0,
    G,
    R
};

class ColorArea
{
  public:
    ColorArea(
        const cv::Mat &img,
        const cv::Point &point_center,
        const cv::Scalar &color,
        const uint32_t markerwidth,
        const uint32_t markerheight,
        const uint8_t tolerance);
    virtual ~ColorArea();
    bool ColorAreaValueWithinTolerance(const cv::Mat &img) const;
    cv::Scalar GetAverageColor(const cv::Mat &img) const;

  protected:
    cv::Mat colorarea_mask;
    cv::Point point_center;
    cv::Range croprange_x;
    cv::Range croprange_y;
    cv::Scalar color;
    cv::Size img_size;
    uint32_t markerwidth;
    uint32_t markerheight;
    uint8_t tolerance;
};

class ColorAreaEllipse : public ColorArea
{
  public:
    ColorAreaEllipse(
        const cv::Mat &img,
        const cv::Point &point_center,
        const cv::Scalar &color,
        const uint32_t markerwidth,
        const uint32_t markerheight,
        const uint8_t tolerance);
};

class ColorAreaRectangle : public ColorArea
{
  public:
    ColorAreaRectangle(
        const cv::Mat &img,
        const cv::Point &point_center,
        const cv::Scalar &color,
        const uint32_t markerwidth,
        const uint32_t markerheight,
        const uint8_t tolerance);
};
