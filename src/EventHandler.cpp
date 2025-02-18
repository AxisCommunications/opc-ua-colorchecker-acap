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
#include <future>
#include <stdexcept>
#include <vector>

#include "EventHandler.hpp"
#include "common.hpp"

using namespace std;

static void declaration_complete(guint declaration, gpointer user_data)
{
    assert(NULL != user_data);
    (void)declaration;

    bool *init = static_cast<bool *>(user_data);
    *init = true;
    LOG_I("%s/%s: Event declaration complete!", __FILE__, __FUNCTION__);
}

EventHandler::EventHandler() : event_handler_(ax_event_handler_new()), initialized_(false)
{
    GError *error = nullptr;

    // Create keys, namespaces, and nice names for the event
    const bool falsebool = false;
    AXEventKeyValueSet *set = ax_event_key_value_set_new();
    // clang-format off
    ax_event_key_value_set_add_key_values(set, &error,
        "topic0", "tnsaxis", "CameraApplicationPlatform", AX_VALUE_TYPE_STRING,
        "topic1", "tnsaxis", "ColorChecker", AX_VALUE_TYPE_STRING,
        "topic2", "tnsaxis", "WithinTolerance", AX_VALUE_TYPE_STRING,
        "active", NULL, &falsebool, AX_VALUE_TYPE_BOOL,
         NULL);
    // clang-format on

    if (nullptr != error)
    {
        LOG_E("%s/%s: Could not add key values: %s", __FILE__, __FUNCTION__, error->message);
        throw runtime_error(error->message);
        g_error_free(error);
    }

    // Set nice names
    ax_event_key_value_set_add_nice_names(set, "topic0", "tnsaxis", NULL, "Application", NULL);
    ax_event_key_value_set_add_nice_names(set, "topic1", "tnsaxis", NULL, "AXIS Color Checker", NULL);
    ax_event_key_value_set_add_nice_names(set, "topic2", "tnsaxis", NULL, "Color Checker", NULL);
    ax_event_key_value_set_add_nice_names(set, "active", NULL, NULL, "Within tolerance", NULL);

    // Mark data value
    ax_event_key_value_set_mark_as_data(set, "active", NULL, NULL);

    // Declare event
    if (!ax_event_handler_declare(
            event_handler_,
            set,
            FALSE, // define stateful event
            &event_id_,
            declaration_complete,
            &initialized_,
            &error))
    {
        LOG_E("%s/%s: Could not declare: %s", __FILE__, __FUNCTION__, error->message);
        throw runtime_error(error->message);
        g_error_free(error);
    }

    // The key/value set is no longer needed
    ax_event_key_value_set_free(set);
}

EventHandler::~EventHandler()
{
    assert(nullptr != event_handler_);
    assert(0 != event_id_);

    LOG_I("%s/%s: Undeclare event ...", __FILE__, __FUNCTION__);
    ax_event_handler_undeclare(event_handler_, event_id_, nullptr);

    LOG_I("%s/%s: Free eventhandler ...", __FILE__, __FUNCTION__);
    ax_event_handler_free(event_handler_);
}

void EventHandler::Send(const gboolean active) const
{
    if (!initialized_)
    {
        LOG_I("%s/%s: Event handling not yet initialized", __FILE__, __FUNCTION__);
        return;
    }

    auto set = ax_event_key_value_set_new();

    // Add the variable elements of the event to the set
    ax_event_key_value_set_add_key_value(set, "active", NULL, &active, AX_VALUE_TYPE_BOOL, NULL);

    // Create the event
    auto event = ax_event_new2(set, NULL);

    // The key/value set is no longer needed
    ax_event_key_value_set_free(set);

    // Send the event
    assert(nullptr != event_handler_);
    ax_event_handler_send_event(event_handler_, event_id_, event, NULL);

    LOG_I("%s/%s: Stateful event (%s tolerance) sent", __FILE__, __FUNCTION__, active ? "within" : "exceeds");

    ax_event_free(event);
}
