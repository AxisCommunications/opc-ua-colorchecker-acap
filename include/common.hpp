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

#include <stdio.h>
#include <syslog.h>

// clang-format off
#define LOG(type, fmt, ...) { syslog(type, fmt, ##__VA_ARGS__); printf(fmt, ##__VA_ARGS__); printf("\n"); }
#define LOG_I(fmt, ...) { LOG(LOG_INFO, fmt, ##__VA_ARGS__) }
#define LOG_E(fmt, ...) { LOG(LOG_ERR, fmt, ##__VA_ARGS__) }
#if defined(DEBUG_WRITE)
#define LOG_D(fmt, ...) { LOG(LOG_DEBUG, fmt, ##__VA_ARGS__) }
#else
#define LOG_D(fmt, ...)
#endif
// clang-format on
