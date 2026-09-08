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
#include <utility>

#include "OpcUaServer.hpp"
#include "common.hpp"

using namespace std;

#define LABEL (char *)"ColorAreaReading"
#define REFRESH_INTERVAL_MS 1000

OpcUaServer::OpcUaServer()
    : serverthread_(nullptr), running_(false), server_(nullptr), colorareavalue_(false), colorareavaluepending_(false),
      lastupdate_(chrono::steady_clock::time_point{})
{
}

OpcUaServer::~OpcUaServer()
{
    ShutDownServer();
}

bool OpcUaServer::LaunchServer(const unsigned int serverport)
{
    lock_guard<mutex> lock(mtx_);

    assert(nullptr == server_);
    assert(nullptr == serverthread_);
    assert(!running_);
    assert(1024 <= serverport && 65535 >= serverport);

    // Create an OPC UA server
    LOG_I("⏳ Creating UA server serving on port %u ...", serverport);
    server_ = UA_Server_new();
    if (nullptr == server_)
    {
        LOG_E("%s/%s: Failed to create new UA_Server", __FILE__, __func__);
        return false;
    }
    const auto config_status = UA_ServerConfig_setMinimal(UA_Server_getConfig(server_), serverport, nullptr);
    if (UA_STATUSCODE_GOOD != config_status)
    {
        LOG_E(
            "%s/%s: Failed configuring UA server on port %u (%s)",
            __FILE__,
            __func__,
            serverport,
            UA_StatusCode_name(config_status));
        UA_Server_delete(exchange(server_, nullptr));
        return false;
    }
    AddBoolean(LABEL, false);

    running_ = true;
    serverthread_ = new thread(this->RunUaServer, this);

    LOG_I("✅ UA server configured for port %u", serverport);

    return true;
}

void OpcUaServer::ShutDownServer()
{
    thread *serverthread = nullptr;
    {
        lock_guard<mutex> lock(mtx_);
        serverthread = exchange(serverthread_, nullptr);
        if (nullptr == serverthread)
        {
            return;
        }
        running_ = false;
    }

    LOG_I("🧹 Request OPC UA server thread stop ...");
    if (serverthread->joinable())
    {
        serverthread->join();
    }
    delete serverthread;
    LOG_I("✅ OPC UA server has been shut down");
}

bool OpcUaServer::IsRunning() const
{
    lock_guard<mutex> lock(mtx_);
    return running_;
}

void OpcUaServer::UpdateColorAreaValue(bool value)
{
    // Even if there is no change, update every REFRESH_INTERVAL_MS millisecond(s);
    // that will bump the timestamp on the server so the client can see if the
    // value is fresh or ancient.
    const auto now = chrono::steady_clock::now();
    lock_guard<mutex> lock(mtx_);
    const auto elapsedtime = now - lastupdate_;
    if (colorareavalue_ != value || chrono::milliseconds(REFRESH_INTERVAL_MS) <= elapsedtime)
    {
        colorareavalue_ = value;
        colorareavaluepending_ = true;
    }
}

bool OpcUaServer::GetColorAreaValue()
{
    lock_guard<mutex> lock(mtx_);
    return colorareavalue_;
}

void OpcUaServer::WriteColorAreaValue(bool value)
{
    assert(nullptr != server_);

    UA_Variant newvalue;
    UA_Variant_setScalar(&newvalue, &value, &UA_TYPES[UA_TYPES_BOOLEAN]);
    UA_NodeId currentNodeId = UA_NODEID_STRING(1, LABEL);
    const auto rc = UA_Server_writeValue(server_, currentNodeId, newvalue);
    if (UA_STATUSCODE_GOOD != rc)
    {
        LOG_E("%s/%s: Failed to set OPC UA color area value (%s)", __FILE__, __func__, UA_StatusCode_name(rc));
    }
    else
    {
        LOG_D("%s/%s: Color area value set to: %s", __FILE__, __func__, value ? "TRUE" : "FALSE");
    }
}

void OpcUaServer::AddBoolean(char *label, UA_Boolean value)
{
    assert(nullptr != server_);
    assert(nullptr != label);

    // Define attributes
    char *enUS = (char *)"en-US";
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    UA_Variant_setScalar(&attr.value, &value, &UA_TYPES[UA_TYPES_BOOLEAN]);
    attr.description = UA_LOCALIZEDTEXT(enUS, label);
    attr.displayName = UA_LOCALIZEDTEXT(enUS, label);
    attr.dataType = UA_TYPES[UA_TYPES_BOOLEAN].typeId;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ;

    // Add the variable node to the information model
    UA_NodeId node_id = UA_NODEID_STRING(1, label);
    UA_QualifiedName name = UA_QUALIFIEDNAME(1, label);
    UA_NodeId parent_node_id = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId parent_ref_node_id = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
    const auto rc = UA_Server_addVariableNode(
        server_,
        node_id,
        parent_node_id,
        parent_ref_node_id,
        name,
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
        attr,
        nullptr,
        nullptr);
    assert(UA_STATUSCODE_GOOD == rc);
}

void OpcUaServer::RunUaServer(OpcUaServer *parent)
{
    assert(nullptr != parent);

    LOG_I("⏳ Starting UA server ...");
    auto status = UA_Server_run_startup(parent->server_);
    while (UA_STATUSCODE_GOOD == status && parent->running_)
    {
        bool colorareavalue = false;
        bool colorareavaluepending = false;
        {
            lock_guard<mutex> lock(parent->mtx_);
            if (!parent->running_ || nullptr == parent->server_)
            {
                break;
            }
            colorareavalue = parent->colorareavalue_;
            colorareavaluepending = exchange(parent->colorareavaluepending_, false);
        }
        if (colorareavaluepending)
        {
            parent->WriteColorAreaValue(colorareavalue);
            lock_guard<mutex> lock(parent->mtx_);
            parent->lastupdate_ = chrono::steady_clock::now();
        }
        UA_Server_run_iterate(parent->server_, true);
    }
    if (UA_STATUSCODE_GOOD == status)
    {
        status = UA_Server_run_shutdown(parent->server_);
    }
    LOG_I("🚪 UA Server exit status is '%s'", UA_StatusCode_name(status));

    lock_guard<mutex> lock(parent->mtx_);
    parent->running_ = false;
    UA_Server_delete(exchange(parent->server_, nullptr));
    return;
}
