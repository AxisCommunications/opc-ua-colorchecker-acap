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
#include <cmath>
#include <iostream>
#include <map>
#include <opencv2/imgproc.hpp>

#include "ColorArea.hpp"
#include "common.hpp"

using namespace cv;
using namespace std;

// Use DEBUG_WRITE to write images to storage for debugging
#if defined(DEBUG_WRITE)
#include <opencv2/imgcodecs.hpp>
#define DBG_WRITE_IMG(filename, img) imwrite(filename, img);
#else
#define DBG_WRITE_IMG(filename, img)
#endif

ColorArea::ColorArea(
    const Mat &img,
    const Point &point_center,
    const Scalar &color,
    const uint32_t markerwidth,
    const uint32_t markerheight,
    const uint8_t tolerance)
    : color_(color), img_size_(img.size()), markerwidth_(markerwidth), markerheight_(markerheight),
      tolerance_(tolerance)
{
    // Crop to avoid processing pixels outside color area
    int max_x = point_center.x + markerwidth / 2;
    int max_y = point_center.y + markerheight / 2;
    int min_x = point_center.x - markerwidth / 2;
    int min_y = point_center.y - markerheight / 2;
    if (img.size().width < max_x)
    {
        max_x = img.size().width;
    }
    if (img.size().height < max_y)
    {
        max_y = img.size().height;
    }
    if (0 > min_x)
    {
        min_x = 0;
    }
    if (0 > min_y)
    {
        min_y = 0;
    }
    croprange_x_ = Range(min_x, max_x);
    croprange_y_ = Range(min_y, max_y);
    const Point offset(croprange_x_.start, croprange_y_.start);
    point_center_ = point_center - offset;
#if defined(DEBUG_WRITE)
    Mat cropped_img = img(croprange_y_, croprange_x_);
    DBG_WRITE_IMG("cropped_img.jpg", cropped_img);
#endif

    LOG_I(
        "%s/%s: img size = %ux%u, marker size = %ux%u, center = (%u, %u), color (R, G, B) = (%.1f, %.1f, %.1f), "
        "tolerance = "
        "%u",
        __FILE__,
        __FUNCTION__,
        img_size_.width,
        img_size_.height,
        markerwidth,
        markerheight,
        point_center.x,
        point_center.y,
        color[R],
        color[G],
        color[B],
        tolerance);
}

ColorArea::~ColorArea()
{
}

Scalar ColorArea::GetAverageColor(const Mat &img) const
{
    // Make sure input image has the same size as the gague was set up for
    assert(img.size() == img_size_);

    // Crop
    const auto crop_img = img(croprange_y_, croprange_x_);
    DBG_WRITE_IMG("avg_after_crop.jpg", crop_img);

    return mean(crop_img, colorarea_mask_);
}

bool ColorArea::ColorAreaValueWithinTolerance(const Mat &img) const
{
    // Make sure input image has the same size as the gague was set up for
    assert(img_size_ == img.size());

    const auto currentavg = GetAverageColor(img);
    LOG_D(
        "%s/%s: Target/Current average color in region: (%1.f, %.1f, %.1f)/(%.1f, %.1f, %.1f)",
        __FILE__,
        __FUNCTION__,
        color_.val[R],
        color_.val[G],
        color_.val[B],
        currentavg.val[R],
        currentavg.val[G],
        currentavg.val[B]);
    const auto colordiff_r = abs(color_.val[R] - currentavg.val[R]);
    const auto colordiff_g = abs(color_.val[G] - currentavg.val[G]);
    const auto colordiff_b = abs(color_.val[B] - currentavg.val[B]);

    return (tolerance_ > colordiff_r && tolerance_ > colordiff_g && tolerance_ > colordiff_b);
}

ColorAreaEllipse::ColorAreaEllipse(
    const Mat &img,
    const Point &point_center,
    const Scalar &color,
    const uint32_t markerwidth,
    const uint32_t markerheight,
    const uint8_t tolerance)
    : ColorArea(img, point_center, color, markerwidth, markerheight, tolerance)
{
#if defined(DEBUG_WRITE)
    // Create a debug image tho show the marker
    const auto marker_img = img.clone();

    // Draw the ellipse
    Size axes(markerwidth / 2, markerheight / 2);
    ellipse(marker_img, point_center, axes, 0, 0, 360, cv::Scalar(0, 0, 0), 3);
    ellipse(marker_img, point_center, axes, 0, 0, 360, cv::Scalar(255, 255, 255), 1);

    DBG_WRITE_IMG("marker_img.jpg", marker_img);
#endif

    // Create color mask
    colorarea_mask_ = Mat::zeros(Size(croprange_x_.size(), croprange_y_.size()), CV_8U);
    ellipse(
        colorarea_mask_,
        point_center_,
        Size(markerwidth / 2, markerheight / 2),
        0.0,
        0,
        360,
        Scalar(255, 255, 255),
        -1,
        LINE_8,
        0);
    DBG_WRITE_IMG("mask_img.jpg", colorarea_mask_);
    LOG_I("%s/%s: Elliptic colorarea created", __FILE__, __FUNCTION__);
}

ColorAreaRectangle::ColorAreaRectangle(
    const Mat &img,
    const Point &point_center,
    const Scalar &color,
    const uint32_t markerwidth,
    const uint32_t markerheight,
    const uint8_t tolerance)
    : ColorArea(img, point_center, color, markerwidth, markerheight, tolerance)
{
#if defined(DEBUG_WRITE)
    // Create a debug image tho show the marker
    const auto marker_img = img.clone();

    // Draw the rectangle
    const auto pt1 = point_center - Point(markerwidth / 2, markerheight / 2);
    const auto pt2 = point_center + Point(markerwidth / 2, markerheight / 2);
    rectangle(marker_img, pt1, pt2, Scalar(0, 0, 0), 3);
    rectangle(marker_img, pt1, pt2, Scalar(255, 255, 255), 1);

    DBG_WRITE_IMG("marker_img.jpg", marker_img);
#endif

    // Create color mask
    colorarea_mask_ = Mat::ones(Size(croprange_x_.size(), croprange_y_.size()), CV_8U) * 255;
    DBG_WRITE_IMG("mask_img.jpg", colorarea_mask_);
    LOG_I("%s/%s: Rectancular colorarea created", __FILE__, __FUNCTION__);
}
