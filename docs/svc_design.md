Services Design
==============

This document describes IPC contracts between various clients and corresponding services of the OCVSMD.

- [Services Design](#services-design)
- [Node services](#node-services)
  - [`ExecCmd`](#execcmd)
  - [`ListRegisters`](#listregisters)
  - [`AccessRegisters`](#accessregisters)
- [Relay services](#relay-services)
  - [`RawPublisher`](#rawpublisher)
  - [`RawRpcClient`](#rawrpcclient)
  - [`RawSubscriber`](#rawsubscriber)
- [File Server services](#file-server-services)
  - [`ListRoots`](#listroots)
  - [`PopRoot`](#poproot)
  - [`PushRoot`](#pushroot)

# Node services

## `ExecCmd`

**DSDL definitions:**
- `ExecCmd.0.2.dsdl`
```
uint64 timeout_us
uint16[<=128] node_ids
UavcanNodeExecCmdReq.0.1 payload
@extent 600 * 8
---
ocvsmd.common.Error.0.1 error
uint16 node_id
UavcanNodeExecCmdRes.0.1 payload
@extent 128 * 8
```

**Sequence diagram**
```mermaid
sequenceDiagram
    actor User
    participant ExecCmdClient as ExecCmdClient<br/><<sender>>>
    participant ExecCmdService
    participant LibCyphal as LibCyphal and<br/>RPC Client
    actor NodeX as NodeX<br/><<cyphal node>>
    
    User ->>+ ExecCmdClient: submit(node_ids, cmd, timeout)
    loop for every 128 node ids
        ExecCmdClient --)+ ExecCmdService: Route{ChMsg{}}<br/>ExecCmd.Request_0_2{part_of_node_ids, payload, timeout}
        loop for each node_id
            ExecCmdService ->>- ExecCmdService: map[node_id] = NodeContext{payload, timeout}
        end
    end
    ExecCmdClient --)+ ExecCmdService: Route{ChEnd{alive=true}}
    ExecCmdClient ->>- User : return

    loop for each distinct node id
        ExecCmdService ->> LibCyphal: makeClient(node_id)
        ExecCmdService ->>- LibCyphal: client.request(deadline, payload)
        LibCyphal --) NodeX: ExecuteCommand_1_3.Request{cmd}
    end
    Note over LibCyphal: waiting for responses from all nodes (success or timeout)...
    loop for each node id
        alt
            NodeX --) LibCyphal: ExecuteCommand_1_3.Response{status}
        else
            LibCyphal -) LibCyphal: timeout or failure
        end
        LibCyphal -)+ ExecCmdService: promise result
        ExecCmdService --)+ ExecCmdClient: Route{ChMsg{}}<br/>ExecCmd.Response_0_2{node_id, err, payload}
        ExecCmdService -x- LibCyphal: release client
        ExecCmdClient ->>- ExecCmdClient: map[node_id] = response
    end
    
    ExecCmdService --) ExecCmdClient: Route{ChEnd{alive=false}}
    ExecCmdClient -) User: receiver(map)

    box SDK Client
        actor User
        participant ExecCmdClient
    end
    box Daemon process<br/>《cyphal node》
        participant ExecCmdService
        participant LibCyphal
    end
    box Cyphal Network Nodes
        actor NodeX
    end
```

## `ListRegisters`

**DSDL definitions:**
- `ListRegisters.0.1.dsdl`
```
uint64 timeout_us
uint16[<=128] node_ids
@extent 600 * 8
---
ocvsmd.common.Error.0.1 error
uint16 node_id
uavcan.register.Name.1.0 item
@extent 600 * 8
```

**Sequence diagram**
```mermaid
sequenceDiagram
    actor User
    participant ListRegistersClient as ListRegistersClient<br/><<sender>>>
    participant ListRegistersService
    participant LibCyphal as LibCyphal and<br/>RPC Client
    actor NodeX as NodeX<br/><<cyphal node>>
    
    User ->>+ ListRegistersClient: submit(node_ids, timeout)
    loop for every 128 node ids
        ListRegistersClient --) ListRegistersService: Route{ChMsg{}}<br/>ListRegisters.Request_0_1{part_of_node_ids, timeout}
        loop for each node_id
            ListRegistersService ->> ListRegistersService: map[node_id] = NodeContext{timeout}
        end
    end
    ListRegistersClient --) ListRegistersService: Route{ChEnd{alive=true}}
    ListRegistersClient ->>- User : return
    
    par in parallel for each distinct node id
        ListRegistersService ->> LibCyphal: node_cnxt.client = makeClient(node_id)
        Note over ListRegistersClient, LibCyphal: Repeat Cyphal "uavcan.register.385.List.1.0" requests incrementing register index<br/>until empty register name result, timeout or failure. Post responses to the client.
        loop while previous result reg name is not empty
            ListRegistersService ->> LibCyphal: client.request(deadline, node_cnxt.reg_index)
            LibCyphal --) NodeX: List::Request_1_0{reg_index}
            alt success
                NodeX --) LibCyphal: List::Response_1_0{reg_name}
                LibCyphal -)+ ListRegistersService: promise result with<br/>non-empty reg name
                ListRegistersService --)+ ListRegistersClient: Route{ChMsg{}}<br/>ListRegisters.Response_0_1{node_id, reg_name}
                ListRegistersClient ->>- ListRegistersClient: map[node_id].append(reg_name)
                ListRegistersService ->>- ListRegistersService: node_cnxt.reg_index++
                Note right of ListRegistersService: continue the async loop for this node
            else end of list
                NodeX --) LibCyphal: List::Response_1_0{""}
                LibCyphal -)+ ListRegistersService: promise result with<br/>empty reg name
                ListRegistersService -x- LibCyphal: release client
                Note right of ListRegistersService: break the async loop for this node 
            else failure
                LibCyphal -) LibCyphal: timeout or failure
                LibCyphal -)+ ListRegistersService: promise failure
                ListRegistersService --)+ ListRegistersClient: Route{ChMsg{}}<br/>ListRegisters.Response_0_1{node_id, error}
                ListRegistersClient ->>- ListRegistersClient: map[node_id].emplace(failure)
                ListRegistersService -x- LibCyphal: release client
                Note right of ListRegistersService: break the async loop for this node
            end
        end
    end
    
    ListRegistersService --) ListRegistersClient: Route{ChEnd{alive=false}}
    ListRegistersClient -) User: receiver(map<vector>)

    box SDK Client
        actor User
        participant ListRegistersClient
    end
    box Daemon process<br/>《cyphal node》
        participant ListRegistersService
        participant LibCyphal
    end
    box Cyphal Network Nodes
        actor NodeX
    end
```

## `AccessRegisters`

**DSDL definitions:**
- `AccessRegisters.0.1.dsdl`
```
@union
uavcan.primitive.Empty.1.0 empty
AccessRegistersScope.0.1 scope
AccessRegistersKeyValue.0.1 register
@sealed
---
ocvsmd.common.Error.0.1 error
uint16 node_id
AccessRegistersKeyValue.0.1 register
@extent 700 * 8
```
- `AccessRegistersScope.0.1.dsdl`
```
uint64 timeout_us
uint16[<=128] node_ids
@extent 600 * 8
```
- `AccessRegistersKeyValue.0.1.dsdl`
```
uavcan.register.Name.1.0 key
uavcan.register.Value.1.0 value
@extent 600 * 8
```

**Sequence diagram**
```mermaid
sequenceDiagram
    actor User
    participant AccessRegistersClient as AccessRegistersClient<br/><<sender>>>
    participant AccessRegistersService
    participant LibCyphal as LibCyphal and<br/>RPC Client
    actor NodeX as NodeX<br/><<cyphal node>>
    
    User ->>+ AccessRegistersClient: submit(node_ids, regs, timeout)
    loop for every 128 node ids
        AccessRegistersClient --) AccessRegistersService: Route{ChMsg{}}<br/>AccessRegisters.Request_0_1{Scope{part_of_node_ids, timeout}}
        loop for each node_id
            AccessRegistersService ->> AccessRegistersService: map[node_id] = NodeContext{timeout}
        end
    end
    loop for each register key
        AccessRegistersClient --) AccessRegistersService: Route{ChMsg{}}<br/>AccessRegisters.Request_0_1{KeyValue{key, value}}
        AccessRegistersService ->> AccessRegistersService: registers.append(key_value)
    end
    AccessRegistersClient --) AccessRegistersService: Route{ChEnd{alive=true}}
    AccessRegistersClient ->>- User : return
    
    par in parallel for each distinct node id
        AccessRegistersService ->> LibCyphal: node_cnxt.client = makeClient(node_id)
        Note over AccessRegistersClient, LibCyphal: Repeat Cyphal "uavcan.register.384.Access.1.0" requests for each register. Post responses to the client.
        loop for each register key & value
            AccessRegistersService ->> LibCyphal: client.request(deadline, reg_key_value)
            LibCyphal --) NodeX: Access::Request_1_0{reg_key, reg_value}
            alt success
                NodeX --) LibCyphal: Access::Response_1_0{reg_value}
                LibCyphal -) AccessRegistersService: promise result
                AccessRegistersService --)+ AccessRegistersClient: Route{ChMsg{}}<br/>AccessRegisters.Response_0_1{node_id, KeyValue{key, value}}
                AccessRegistersClient ->>- AccessRegistersClient: map[node_id].emplace_back(reg_key, reg_value)
            else failure
                LibCyphal -) LibCyphal: timeout or failure
                LibCyphal -) AccessRegistersService: promise failure
                AccessRegistersService --)+ AccessRegistersClient: Route{ChMsg{}}<br/>AccessRegisters.Response_0_1{node_id, error, KeyValue{key, <<empty>>}}
                AccessRegistersClient ->>- AccessRegistersClient: map[node_id].emplace_back(reg_key, failure)
            end
            Note over AccessRegistersService: continue the async loop for this node
        end
        AccessRegistersService -x LibCyphal: release client
    end
    
    AccessRegistersService --) AccessRegistersClient: Route{ChEnd{alive=false}}
    AccessRegistersClient -) User: receiver(map<vector>)

    box SDK Client
        actor User
        participant AccessRegistersClient
    end
    box Daemon process<br/>《cyphal node》
        participant AccessRegistersService
        participant LibCyphal
    end
    box Cyphal Network Nodes
        actor NodeX
    end
```

# Relay services

## `RawPublisher`

**DSDL definitions:**
- `RawPublisher.0.1.dsdl`
```
@union
uavcan.primitive.Empty.1.0 empty
RawPublisherCreate.0.1 create
RawPublisherConfig.0.1 config
RawPublisherPublish.0.1 publish
@sealed
---
@union
uavcan.primitive.Empty.1.0 empty
ocvsmd.common.Error.0.1 publish_error
@sealed
```
- `RawPublisherCreate.0.1.dsdl`
```
uint16 subject_id
@extent 16 * 8
```
- `RawPublisherConfig.0.1.dsdl`
```
uint8[<=1] priority
@extent 32 * 8
```
- `RawPublisherPublish.0.1.dsdl`
```
uint64 timeout_us
uint64 payload_size
@extent 32 * 8
```

**Sequence diagram**
```mermaid
sequenceDiagram
    actor User
    participant Publisher as Publisher<br/><<sender>>>
    participant RawPublisherClient as RawPublisherClient<br/><<sender>>>
    participant RawPublisherService
    participant CyPublisher as LibCyphal<br/>RawPublisher
    actor NodeX as NodeX<br/><<cyphal node>>

    Note over Publisher, CyPublisher: Creating of a Cyphal Network Publisher.
    User ->>+ RawPublisherClient: submit(subj_id)
    RawPublisherClient --)+ RawPublisherService: Route{ChMsg{}}<br/>RawPublisher.Request_0_1{Create{subj_id}}
    RawPublisherClient ->>- User : return
    
    RawPublisherService ->> CyPublisher: pub = create.publisher<void>(subj_id)
    activate RawPublisherClient
    alt success
        RawPublisherService --) RawPublisherClient: Route{ChMsg{}}<br/>RawPublisher.Response_0_1{empty}
        RawPublisherClient ->> Publisher: create(move(channel))
        Note right of RawPublisherClient: The client has fulfilled its "factory" role, and<br/>now the Publisher continues with the channel.
    else failure
        RawPublisherService --) RawPublisherClient: Route{ChEnd{alive=false, error}}
    end
    deactivate RawPublisherService
    RawPublisherClient -)- User: receiver(publisher_or_failure)
    
    Note over Publisher, CyPublisher: Publishing messages to Cyphal Network. Changing message priorities.
    loop while keeping the publisher alive
        alt publishing
            User ->>+ Publisher: publish<Msg>(msg, timeout)
            Publisher ->> Publisher: rawPublish(raw_payload, timeout)
            Publisher --)+ RawPublisherService: Route{ChMsg{}}<br/>RawPublisher.Request_0_1{Publish{payload_size, timeout}}<br/>raw_payload
            Publisher ->>- User: return
            RawPublisherService ->>+ CyPublisher: pub.publish(raw_payload, timeout)
            CyPublisher --) NodeX: Message<subj_id>{}
            Note left of NodeX: Cyphal network subscriber(s)<br/>receive the message
            CyPublisher ->>- RawPublisherService: result
            RawPublisherService --)+ Publisher: Route{ChMsg{}}<br/>RawPublisher.Response_0_1{opt_error}
            deactivate RawPublisherService
            Publisher -)- User: receiver(opt_error)
        else configuring priority
            User ->>+ Publisher: setPriority(priority)
            Publisher --)+ RawPublisherService: Route{ChMsg{}}<br/>RawPublisher.Request_0_1{Config{priority}}
            Publisher ->>- User: opt_error
            opt !priority.empty
                RawPublisherService ->> CyPublisher: pub.setPriority(priority.front)
            end
            deactivate RawPublisherService
        end
    end

    Note over Publisher, CyPublisher: Releasing the Cyphal Network Publisher.
    User -x+ Publisher: release
    Publisher --)+ RawPublisherService: Route{ChEnd{alive=false}}
    deactivate Publisher
    RawPublisherService -x CyPublisher: release pub
    deactivate RawPublisherService

    box SDK Client
        actor User
        participant Publisher
        participant RawPublisherClient
    end
    box Daemon process<br/>《cyphal node》
        participant RawPublisherService
        participant CyPublisher
    end
    box Cyphal Network Nodes
        actor NodeX
    end
```

## `RawRpcClient`

**DSDL definitions:**
- `RawRpcClient.0.1.dsdl`
```
@union
uavcan.primitive.Empty.1.0 empty
RawRpcClientCreate.0.1 create
RawRpcClientConfig.0.1 config
RawRpcClientCall.0.1 call
@sealed
---
@union
uavcan.primitive.Empty.1.0 empty
RawRpcClientReceive.0.1 receive
ocvsmd.common.Error.0.1 error
@sealed
```
- `RawRpcClientCreate.0.1.dsdl`
```
uint64 extent_size
uint16 service_id
uint16 server_node_id
@extent 32 * 8
```
- `RawRpcClientConfig.0.1.dsdl`
```
uint8[<=1] priority
@extent 32 * 8
```
- `RawRpcClientCall.0.1.dsdl`
```
uint64 request_timeout_us
uint64 response_timeout_us
uint64 payload_size
@extent 64 * 8
```
- `RawRpcClientReceive.0.1.dsdl`
```
uint8 priority
uint16 remote_node_id
uint64 payload_size
@extent 32 * 8
```

**Sequence diagram**
```mermaid
sequenceDiagram
    actor User
    participant RpcClient as RpcClient<br/><<sender>>>
    participant RawRpcClient as RawRpcClient<br/><<sender>>>
    participant RawRpcClientService
    participant CyServiceClient as LibCyphal<br/>RawServiceClient
    actor NodeX as NodeX<br/><<cyphal node>>

    Note over RpcClient, CyServiceClient: Creating of a Cyphal Network RpcClient.
    User ->>+ RawRpcClient: submit(service_id, extent, srv_node_id)
    RawRpcClient --)+ RawRpcClientService: Route{ChMsg{}}<br/>RawRpcClient.Request_0_1{Create{service_id, extent, srv_node_id}}
    RawRpcClient ->>- User : return
    
    RawRpcClientService ->> CyServiceClient: client = makeClient(srv_node_id, service_id, extent)
    activate RawRpcClient
    alt success
        RawRpcClientService --) RawRpcClient: Route{ChMsg{}}<br/>RawRpcClient.Response_0_1{empty}
        RawRpcClient ->> RpcClient: create(move(channel))
        Note right of RawRpcClient: The client has fulfilled its "factory" role, and<br/>now the RpcClient continues with the channel.
    else failure
        RawRpcClientService --) RawRpcClient: Route{ChEnd{alive=false, error}}
    end
    deactivate RawRpcClientService
    RawRpcClient -)- User: receiver(rpc_client_or_failure)
    
    Note over RpcClient, CyServiceClient: Making requests to Cyphal Network, receiving responses, and changing request priorities.
    loop while keeping the rpc client alive
        alt making requests
        
            %% Request
            %%
            Note over RawRpcClient, RawRpcClientService: Making a request
            User ->>+ RpcClient: request<Msg>(msg, timeouts)
            RpcClient ->> RpcClient: rawRequest(raw_payload, timeouts)
            RpcClient --)+ RawRpcClientService: Route{ChMsg{}}<br/>RawRpcClient.Request_0_1{Call{payload_size, timeouts}}<br/>raw_payload
            RpcClient ->>- User: return
            RawRpcClientService ->>+ CyServiceClient: promise = client.request(raw_payload, timeouts)
            CyServiceClient --) NodeX: SvcRequest<service_id>{}
            Note left of NodeX: A cyphal network server<br/>receive the service request.
            CyServiceClient ->>- RawRpcClientService: promise
            opt if error
            RawRpcClientService --)+ RpcClient: Route{ChMsg{}}<br/>RawRpcClient.Response_0_1{request_error}
            deactivate RawRpcClientService
            opt there is a receiver
                Note right of RpcClient: failure will be dropped<br/>if there is no receiver
                RpcClient -)- User: receiver<Result>(failure)
            end
            end

            %% Response
            %%
            Note over RawRpcClient, RawRpcClientService: Waiting for response...
            alt
                NodeX --) CyServiceClient: SvcResponse<service_id>{}
            else
                CyServiceClient -) CyServiceClient: timeout or failure
            end            
            CyServiceClient ->>+ RawRpcClientService: promise result
            alt success
                RawRpcClientService --)+ RpcClient: Route{ChMsg{}}<br/>RawRpcClient.Response_0_1{Receive{size, meta}}}<br/>raw_payload
                RpcClient ->> RpcClient: response.deserialize(raw_payload)
            else failure
                RawRpcClientService --) RpcClient: Route{ChMsg{}}<br/>RawRpcClient.Response_0_1{response_error}}
            end
            deactivate RawRpcClientService
            opt there is a receiver
                Note right of RpcClient: result will be dropped<br/>if there is no receiver
                RpcClient -)- User: receiver<Result>(result)
            end            
        else configuring priority
            User ->>+ RpcClient: setPriority(priority)
            RpcClient --)+ RawRpcClientService: Route{ChMsg{}}<br/>RawRpcClient.Request_0_1{Config{priority}}
            RpcClient ->>- User: opt_error
            opt !priority.empty
                RawRpcClientService ->> CyServiceClient: client.setPriority(priority.front)
            end
            deactivate RawRpcClientService
        end
    end

    Note over RpcClient, CyServiceClient: Releasing the Cyphal Network RpcClient.
    User -x+ RpcClient: release
    RpcClient --)+ RawRpcClientService: Route{ChEnd{alive=false}}
    deactivate RpcClient
    RawRpcClientService -x CyServiceClient: release client
    deactivate RawRpcClientService

    box SDK Client
        actor User
        participant RpcClient
        participant RawRpcClient
    end
    box Daemon process<br/>《cyphal node》
        participant RawRpcClientService
        participant CyServiceClient
    end
    box Cyphal Network Nodes
        actor NodeX
    end
```

## `RawSubscriber`

**DSDL definitions:**
- `RawSubscriber.0.1.dsdl`
```
@union
uavcan.primitive.Empty.1.0 empty
RawSubscriberCreate.0.1 create
@sealed
---
@union
uavcan.primitive.Empty.1.0 empty
RawSubscriberReceive.0.1 receive
@sealed
```
- `RawSubscriberCreate.0.1.dsdl`
```
uint64 extent_size
uint16 subject_id
@extent 32 * 8
```
- `RawSubscriberReceive.0.1.dsdl`
```
uint8 priority
uint16[<=1] remote_node_id
uint64 payload_size
@extent 64 * 8
```

**Sequence diagram**
```mermaid
sequenceDiagram
    actor User
    participant Subscriber as Subscriber<br/><<sender>>>
    participant RawSubscriberClient as RawSubscriberClient<br/><<sender>>>
    participant RawSubscriberService
    participant CySubscriber as LibCyphal<br/>RawSubscriber
    actor NodeX as NodeX<br/><<cyphal node>>

    Note over Subscriber, CySubscriber: Creating of a Cyphal Network Subscriber.
    User ->>+ RawSubscriberClient: submit(subj_id, extent_size)
    RawSubscriberClient --)+ RawSubscriberService: Route{ChMsg{}}<br/>RawSubscriber.Request_0_1{Create{subj_id, extent}}
    RawSubscriberClient ->>- User : return
    
    RawSubscriberService ->> CySubscriber: sub = create.subscriber<void>(subj_id, extent)
    activate RawSubscriberClient
    alt success
        RawSubscriberService --) RawSubscriberClient: Route{ChMsg{}}<br/>RawSubscriber.Response_0_1{empty}
        RawSubscriberClient ->> Subscriber: create(move(channel))
        Note right of RawSubscriberClient: The client has fulfilled its "factory" role, and<br/>now the Subscriber continues with the channel.
    else failure
        RawSubscriberService --) RawSubscriberClient: Route{ChEnd{alive=false, error}}
    end
    deactivate RawSubscriberService
    RawSubscriberClient -)- User: receiver(subscriber_or_failure)
    
    Note over Subscriber, CySubscriber: Receiving Cyphal Network messages...
    loop while keeping the subscriber alive
        opt    
            User ->>+ Subscriber: receive<Msg>()
            Subscriber ->> Subscriber: rawReceive()
            Subscriber ->>- User: return<Msg>
        end
        NodeX --)+ CySubscriber: Message<subj_id>{}
        Note left of NodeX: A cyphal network publisher<br/>has posted a message
        CySubscriber ->>- RawSubscriberService: handleNodeMessage(raw_payload)
        activate RawSubscriberService
        RawSubscriberService --)+ Subscriber: Route{ChMsg{}}<br/>RawSubscriber.Response_0_1{Receive{size, meta}}<br/>raw_payload
        deactivate RawSubscriberService
        Subscriber ->> Subscriber: msg.deserialize(raw_payload)
        opt there is a receiver
            Note right of Subscriber: `msg` will be dropped<br/>if there is no receiver
            Subscriber -)- User: receiver<Msg>(msg, meta)
        end
    end

    Note over Subscriber, CySubscriber: Releasing the Cyphal Network Subscriber.
    User -x+ Subscriber: release
    Subscriber --)+ RawSubscriberService: Route{ChEnd{alive=false}}
    deactivate Subscriber
    RawSubscriberService -x CySubscriber: release sub
    deactivate RawSubscriberService

    box SDK Client
        actor User
        participant Subscriber
        participant RawSubscriberClient
    end
    box Daemon process<br/>《cyphal node》
        participant RawSubscriberService
        participant CySubscriber
    end
    box Cyphal Network Nodes
        actor NodeX
    end
```

# File Server services

## `ListRoots`

**DSDL definitions:**
- `ListRoots.0.1.dsdl`
```
@extent 512 * 8
---
uavcan.file.Path.2.0 item
@extent 512 * 8
```

**Sequence diagram**
```mermaid
sequenceDiagram
    actor User
    participant ListRootsClient as ListRootsClient<br/><<sender>>>
    participant ListRootsService
    participant FileProvider

    User ->>+ ListRootsClient: submit()
    ListRootsClient --)+ ListRootsService: Route{ChMsg{}}<br/>ListRoots.Request_0_1{}}
    ListRootsClient ->>- User : return
    
    ListRootsService ->> FileProvider: getListOfRoots()
    loop for each root path
        ListRootsService --)+ ListRootsClient: Route{ChMsg{}}<br/>ListRoots.Response_0_1{path}
        ListRootsClient ->>- ListRootsClient: paths.emplace_back(path)
    end
    ListRootsService --)+ ListRootsClient: Route{ChEnd{alive=false}}
    deactivate ListRootsService
    ListRootsClient -)- User: receiver(paths)
    
    box SDK Client
        actor User
        participant ListRootsClient
    end
    box Daemon process<br/>《cyphal node》
        participant ListRootsService
        participant FileProvider
    end    
```

## `PopRoot`

**DSDL definitions:**
- `PopRoot.0.1.dsdl`
```
uavcan.file.Path.2.0 item   # The path to the root directory to be pop from the list of roots.
bool is_back                # Determines whether the path is searched from the front or the back of the list.
@extent 512 * 8
---
@extent 512 * 8
```

**Sequence diagram**
```mermaid
sequenceDiagram
    actor User
    participant PopRootClient as PopRootClient<br/><<sender>>>
    participant PopRootService
    participant FileProvider

    User ->>+ PopRootClient: submit(path, is_back)
    PopRootClient --)+ PopRootService: Route{ChMsg{}}<br/>PopRoot.Request_0_1{path, is_back}}
    PopRootClient ->>- User : return
    
    PopRootService ->> FileProvider: popRoot(path, is_back)
    PopRootService --)+ PopRootClient: Route{ChEnd{alive=false}}
    deactivate PopRootService
    PopRootClient -)- User: receiver(paths)
    
    box SDK Client
        actor User
        participant PopRootClient
    end
    box Daemon process<br/>《cyphal node》
        participant PopRootService
        participant FileProvider
    end    
```

## `PushRoot`

**DSDL definitions:**
- `PushRoot.0.1.dsdl`
```
uavcan.file.Path.2.0 item   # The path to the root directory to be pushed into the list of roots.
bool is_back                # Determines whether the path is searched from the front or the back of the list.
@extent 512 * 8
---
@extent 512 * 8
```

**Sequence diagram**
```mermaid
sequenceDiagram
    actor User
    participant PushRootClient as PushRootClient<br/><<sender>>>
    participant PushRootService
    participant FileProvider

    User ->>+ PushRootClient: submit(path, is_back)
    PushRootClient --)+ PushRootService: Route{ChMsg{}}<br/>PushRoot.Request_0_1{path, is_back}}
    PushRootClient ->>- User : return
    
    PushRootService ->> FileProvider: pushRoot(path, is_back)
    PushRootService --)+ PushRootClient: Route{ChEnd{alive=false}}
    deactivate PushRootService
    PushRootClient -)- User: receiver(paths)
    
    box SDK Client
        actor User
        participant PushRootClient
    end
    box Daemon process<br/>《cyphal node》
        participant PushRootService
        participant FileProvider
    end    
```
