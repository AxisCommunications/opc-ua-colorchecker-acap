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

#pragma once

#include <chrono>
#include <mutex>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <thread>

class OpcUaServer
{
  public:
    OpcUaServer();
    ~OpcUaServer();
    bool LaunchServer(const unsigned int port);
    void ShutDownServer();
    bool IsRunning() const;
    void UpdateColorAreaValue(bool value);
    bool GetColorAreaValue(void);

  protected:
  private:
    void AddBoolean(char *label, UA_Boolean value);
    static void RunUaServer(OpcUaServer *parent);
    bool colorareavalue_;
    std::chrono::steady_clock::time_point lastupdate_;
    std::mutex mtx_;
    std::thread *serverthread_;
    UA_Boolean running_;
    UA_Server *server_;
};
