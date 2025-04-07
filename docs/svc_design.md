# Services Design

This document describes IPC contracts between various clients and corresponding services of the OCVSMD.

## Node services

### `ExecCmd` service

#### DSDL definitions

##### `ExecCmd.0.2.dsdl`
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
#### Sequence diagram
```mermaid
sequenceDiagram
    actor User
    participant ExecCmdClient
    participant ExecCmdService
    participant LibCyphal as LibCyphal and<br/>RPC Client
    actor NodeX
    
    User ->>+ ExecCmdClient: submit(node_ids,cmd,timeout)
    loop for every 128 node ids
        ExecCmdClient --)+ ExecCmdService: ExecCmd.Request(part_of_node_ids, payload, timeout)
        loop for each node_id
            ExecCmdService ->>- ExecCmdService: map[node_id] = (payload, timeout)
        end
    end
    
    ExecCmdClient --)+ ExecCmdService: RouteChEnd{alive=true}
    ExecCmdClient ->>- User : 
    loop for each node id
        ExecCmdService ->> LibCyphal: makeClient(node_id)
        ExecCmdService ->>- LibCyphal: client.request(deadline, payload)
        LibCyphal --) NodeX: ExecuteCommand_1_3.Request{cmd}
    end
    Note over LibCyphal: waiting for responses (success or timeout)...
    loop for each node id
        alt
            NodeX --) LibCyphal: ExecuteCommand_1_3.Response{status}
        else
            LibCyphal -) LibCyphal: timeout
        end
        LibCyphal -)+ ExecCmdService: promise.result
        ExecCmdService --)+ ExecCmdClient: ExecCmd.Response{node_id, err, payload}
        ExecCmdService -x- LibCyphal: release client
        ExecCmdClient ->>- ExecCmdClient: map[node_id] = response
    end
    
    ExecCmdService --) ExecCmdClient: RouteChEnd{alive=false}
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

### `ListRegisters`

### `AccessRegisters`

## Relay services

### `RawPublisher`

### `RasSubscriber`

## File Server services

### `ListRoots`

### `PopRoot`

### `PushRoot`
