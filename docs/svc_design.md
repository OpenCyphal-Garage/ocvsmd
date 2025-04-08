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
    participant ExecCmdClient
    participant ExecCmdService
    participant LibCyphal as LibCyphal and<br/>RPC Client
    actor NodeX
    
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
    ExecCmdClient -) User: result{map}

    box SDK Client
        actor User
        participant ExecCmdClient
    end
    box Daemon
        participant ExecCmdService
        participant LibCyphal
    end
    box Cyphal Network
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
    participant ListRegistersClient
    participant ListRegistersService
    participant LibCyphal as LibCyphal and<br/>RPC Client
    actor NodeX
    
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
            alt
                NodeX --) LibCyphal: List::Response_1_0{reg_name}
                LibCyphal -)+ ListRegistersService: promise result with<br/>non-empty reg name
                ListRegistersService --)+ ListRegistersClient: Route{ChMsg{}}<br/>ListRegisters.Response_0_1{node_id, reg_name}
                ListRegistersClient ->>- ListRegistersClient: map[node_id].append(reg_name)
                ListRegistersService ->>- ListRegistersService: node_cnxt.reg_index++
                Note right of ListRegistersService: continue the async loop for this node
            else
                NodeX --) LibCyphal: List::Response_1_0{""}
                LibCyphal -)+ ListRegistersService: promise result with<br/>empty reg name
                ListRegistersService -x- LibCyphal: release client
                Note right of ListRegistersService: break the async loop for this node 
            else
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
    ListRegistersClient -) User: result{map<vector>}

    box SDK Client
        actor User
        participant ListRegistersClient
    end
    box Daemon
        participant ListRegistersService
        participant LibCyphal
    end
    box Cyphal Network
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
    participant AccessRegistersClient
    participant AccessRegistersService
    participant LibCyphal as LibCyphal and<br/>RPC Client
    actor NodeX
    
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
            alt
                NodeX --) LibCyphal: Access::Response_1_0{reg_value}
                LibCyphal -) AccessRegistersService: promise result
                AccessRegistersService --)+ AccessRegistersClient: Route{ChMsg{}}<br/>AccessRegisters.Response_0_1{node_id, KeyValue{key, value}}
                AccessRegistersClient ->>- AccessRegistersClient: map[node_id].emplace_back(reg_key, reg_value)
            else
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
    AccessRegistersClient -) User: result{map<vector>}

    box SDK Client
        actor User
        participant AccessRegistersClient
    end
    box Daemon
        participant AccessRegistersService
        participant LibCyphal
    end
    box Cyphal Network
        actor NodeX
    end
```

# Relay services

## `RawPublisher`

## `RawSubscriber`

# File Server services

## `ListRoots`

## `PopRoot`

## `PushRoot`
