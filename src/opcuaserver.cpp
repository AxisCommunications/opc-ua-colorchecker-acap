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

#include <assert.h>

#include "common.hpp"
#include "opcuaserver.hpp"

using namespace std;

#define LABEL (char *)"ColorAreaReading"

OpcUaServer::OpcUaServer() : serverthread(nullptr), running(false), server(nullptr)
{
}

OpcUaServer::~OpcUaServer()
{
}

bool OpcUaServer::LaunchServer(const unsigned int serverport)
{
    LOG_I("%s/%s: port %u", __FILE__, __FUNCTION__, serverport);
    assert(nullptr == server);
    assert(nullptr == serverthread);
    assert(!running);
    assert(1024 <= serverport && 65535 >= serverport);

    // Create an OPC UA server
    LOG_I("%s/%s: Create UA server serving on port %u", __FILE__, __FUNCTION__, serverport);
    server = UA_Server_new();
    if (nullptr == server)
    {
        LOG_E("%s/%s: Failed to create new UA_Server", __FILE__, __FUNCTION__);
        return false;
    }
    UA_ServerConfig_setMinimal(UA_Server_getConfig(server), serverport, nullptr);
    AddBoolean(LABEL, false);

    serverthread = new thread(this->RunUaServer, this);

    return true;
}

void OpcUaServer::ShutDownServer()
{
    assert(running);
    assert(nullptr != serverthread);

    LOG_I("%s/%s: Shutting down UA server ...", __FILE__, __FUNCTION__);
    running = false;
    if (nullptr != serverthread)
    {
        if (serverthread->joinable())
        {
            serverthread->join();
        }
        delete serverthread;
        serverthread = nullptr;
    }
    assert(nullptr == server);
    LOG_I("%s/%s: UA server has been shut down", __FILE__, __FUNCTION__);
}

bool OpcUaServer::IsRunning() const
{
    if (running)
    {
        assert(nullptr != server);
        assert(nullptr != serverthread);
    }
    else
    {
        assert(nullptr == server);
        assert(nullptr == serverthread);
    }
    return running;
}

void OpcUaServer::UpdateColorAreaValue(bool value)
{
    if (nullptr == server)
    {
        return;
    }
    // Always update value even if there is no change; that will bump the
    // timestamp on the server so the client can see if the value is fresh or
    // ancient.
    UA_Variant newvalue;
    UA_Variant_setScalar(&newvalue, &value, &UA_TYPES[UA_TYPES_BOOLEAN]);
    UA_NodeId currentNodeId = UA_NODEID_STRING(1, LABEL);
    UA_Server_writeValue(server, currentNodeId, newvalue);
    LOG_D("%s%s: Color area value set to: %s", __FILE__, __FUNCTION__, value ? "TRUE" : "FALSE");
}

bool OpcUaServer::GetColorAreaValue()
{
    assert(nullptr != server);

    UA_NodeId currentNodeId = UA_NODEID_STRING(1, LABEL);

    UA_Variant value;
    UA_Variant_init(&value);
    UA_Server_readValue(server, currentNodeId, &value);

    assert(UA_Variant_isScalar(&value));
    assert(UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_BOOLEAN]));

    bool boolval = *(static_cast<bool *>(value.data));
    UA_Variant_clear(&value);

    return boolval;
}

void OpcUaServer::AddBoolean(char *label, UA_Boolean value)
{
    assert(nullptr != server);
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
    UA_Server_addVariableNode(
        server,
        node_id,
        parent_node_id,
        parent_ref_node_id,
        name,
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
        attr,
        nullptr,
        nullptr);
}

void OpcUaServer::RunUaServer(OpcUaServer *parent)
{
    assert(nullptr != parent);
    assert(nullptr != parent->server);
    assert(false == parent->running);

    LOG_I("%s/%s: Starting UA server ...", __FILE__, __FUNCTION__);
    parent->running = true;
    UA_StatusCode status = UA_Server_run(parent->server, &parent->running);
    LOG_I("%s/%s: UA Server exit status: %s", __FILE__, __FUNCTION__, UA_StatusCode_name(status));
    UA_Server_delete(parent->server);
    parent->server = nullptr;
    return;
}
