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

#include "OpcUaServer.hpp"
#include "common.hpp"

using namespace std;

#define LABEL (char *)"ColorAreaReading"
#define REFRESH_INTERVAL_MS 1000

OpcUaServer::OpcUaServer()
    : colorareavalue_(false), lastupdate_(chrono::steady_clock::time_point{}), serverthread_(nullptr), running_(false),
      server_(nullptr)
{
}

OpcUaServer::~OpcUaServer()
{
}

bool OpcUaServer::LaunchServer(const unsigned int serverport)
{
    LOG_I("%s/%s: port %u", __FILE__, __FUNCTION__, serverport);
    assert(nullptr == server_);
    assert(nullptr == serverthread_);
    assert(!running_);
    assert(1024 <= serverport && 65535 >= serverport);

    // Create an OPC UA server
    LOG_I("%s/%s: Create UA server serving on port %u", __FILE__, __FUNCTION__, serverport);
    server_ = UA_Server_new();
    if (nullptr == server_)
    {
        LOG_E("%s/%s: Failed to create new UA_Server", __FILE__, __FUNCTION__);
        return false;
    }
    UA_ServerConfig_setMinimal(UA_Server_getConfig(server_), serverport, nullptr);
    AddBoolean(LABEL, false);

    serverthread_ = new thread(this->RunUaServer, this);

    return true;
}

void OpcUaServer::ShutDownServer()
{
    assert(running_);
    assert(nullptr != serverthread_);

    LOG_I("%s/%s: Shutting down UA server ...", __FILE__, __FUNCTION__);
    running_ = false;
    if (nullptr != serverthread_)
    {
        if (serverthread_->joinable())
        {
            serverthread_->join();
        }
        delete serverthread_;
        serverthread_ = nullptr;
    }
    assert(nullptr == server_);
    LOG_I("%s/%s: UA server has been shut down", __FILE__, __FUNCTION__);
}

bool OpcUaServer::IsRunning() const
{
    if (running_)
    {
        assert(nullptr != server_);
        assert(nullptr != serverthread_);
    }
    else
    {
        assert(nullptr == server_);
        assert(nullptr == serverthread_);
    }
    return running_;
}

void OpcUaServer::UpdateColorAreaValue(bool value)
{
    if (nullptr == server_)
    {
        return;
    }
    // Even if there is no change, update every REFRESH_INTERVAL_MS millisecond(s);
    // that will bump the timestamp on the server so the client can see if the
    // value is fresh or ancient.
    const auto now = chrono::steady_clock::now();
    lock_guard<mutex> lock(mtx_);
    const auto elapsedtime = now - lastupdate_;
    if (colorareavalue_ != value || chrono::milliseconds(REFRESH_INTERVAL_MS) <= elapsedtime)
    {
        UA_Variant newvalue;
        UA_Variant_setScalar(&newvalue, &value, &UA_TYPES[UA_TYPES_BOOLEAN]);
        UA_NodeId currentNodeId = UA_NODEID_STRING(1, LABEL);
        const auto rc = UA_Server_writeValue(server_, currentNodeId, newvalue);
        if (UA_STATUSCODE_GOOD != rc)
        {
            LOG_E("%s/%s: Failed to set OPC UA color area value (%s)", __FILE__, __FUNCTION__, UA_StatusCode_name(rc));
        }
        else
        {
            LOG_D("%s/%s: Color area value set to: %s", __FILE__, __FUNCTION__, value ? "TRUE" : "FALSE");
        }
        colorareavalue_ = value;
        lastupdate_ = now;
    }
}

bool OpcUaServer::GetColorAreaValue()
{
    assert(nullptr != server_);

    UA_NodeId currentNodeId = UA_NODEID_STRING(1, LABEL);

    UA_Variant value;
    UA_Variant_init(&value);
    const auto rc = UA_Server_readValue(server_, currentNodeId, &value);

    assert(rc == UA_STATUSCODE_GOOD);
    assert(UA_Variant_isScalar(&value));
    assert(UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_BOOLEAN]));

    bool boolval = *(static_cast<bool *>(value.data));
    UA_Variant_clear(&value);

    return boolval;
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
    assert(nullptr != parent->server_);
    assert(false == parent->running_);

    LOG_I("%s/%s: Starting UA server ...", __FILE__, __FUNCTION__);
    parent->running_ = true;
    UA_StatusCode status = UA_Server_run(parent->server_, &parent->running_);
    LOG_I("%s/%s: UA Server exit status: %s", __FILE__, __FUNCTION__, UA_StatusCode_name(status));
    UA_Server_delete(parent->server_);
    parent->server_ = nullptr;
    return;
}
