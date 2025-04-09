# Firmware Update Sequence diagram

Firmware update scenario consists of two main stages:
- Initiating the firmware update
- Downloading the firmware file

These two stages are almost independent, thought some parameters are passed between them.

## Switching Cyphal nodes to "update" mode

To update the firmware, the nodes must be switched to "update" mode.
This is done by sending the `COMMAND_BEGIN_SOFTWARE_UPDATE` command to each node using the `435.ExecuteCommand` RPC service.
The command triggers a target node to switch to "update" mode and prepare for the firmware update.
As part of the preparation process, the node remembers the path to the firmware file and the source node ID of the daemon that will provide the firmware file.
After that, the firmware updating process runs without any further interaction with the original user, who initiated the update.

```mermaid
sequenceDiagram
    autonumber
    actor User
    participant NodeCommandClient as NodeCommandClient<br/>and its internal `ExecCmdClient`
    participant ExecCmdService
    participant CyExecCmdClient as Cyphal<br/>`435.ExecuteCommand.1.3`<br/>RPC client per node
    actor NodeX as NodeX<br/><<cyphal node>>
    
    Note over NodeCommandClient, CyExecCmdClient: Initiating a firmware update.
    User ->>+ NodeCommandClient: beginSoftwareUpdate(node_ids, path)
    NodeCommandClient --) ExecCmdService: * Route{ChMsg{}}<br/>ExecCmd.Request_0_2{<br/>node_ids, cmd{path}, timeout}
    NodeCommandClient --) ExecCmdService: Route{ChEnd{alive=true}}
    deactivate NodeCommandClient
    Note over ExecCmdService, NodeX: Switching nodes to "update" mode.
    par in parallel for each node id
        ExecCmdService ->>+ CyExecCmdClient: rpcClientX = create(node_id)
        ExecCmdService ->> CyExecCmdClient: request({BEGIN_SOFTWARE_UPDATE, path}, timeout})
        CyExecCmdClient --)+ NodeX: uavcan::node::<br/>435.ExecuteCommand.<br/>Request_1_3{cmd, path}
        deactivate CyExecCmdClient
        NodeX --)+ CyExecCmdClient: uavcan::node::<br/>435.ExecuteCommand.<br/>Response_1_3{status}
        Note right of NodeX: The node<br/>has been switched<br/>to "update" mode.<br/><br/>Remember `path` and<br/>src_node_id (of the daemon).
        CyExecCmdClient ->>- ExecCmdService: response({status})
        ExecCmdService --) NodeCommandClient: Route{ChMsg{}}<br/>ExecCmd.Response_0_2{node_id, status}
    end
    ExecCmdService --)+ NodeCommandClient: Route{ChEnd{alive=false}}
    NodeCommandClient -)- User: result{map<status_or_failure>}
    deactivate NodeX

    box SDK Client
        actor User
        participant NodeCommandClient
    end
    box Daemon process<br/>《cyphal node》
        participant ExecCmdService
        participant CyExecCmdClient
    end
    box Cyphal Network Nodes
        actor NodeX
    end
```
Here are descriptions for arrows in the diagram:
1. `User` sends a command to the `NodeCommandClient` to begin the firmware update on the specified nodes, with the specified path to the firmware file.
2. `NodeCommandClient` sends corresponding IPC request(s) to the `ExecCmdService` to build full scope of the update process.
3. `NodeCommandClient` triggers the update process on the remote `ExecCmdService` by marking the end of the channel (with keeping it alive).
4. For each target node id a new Cyphal RTP client is created for standard `435.ExecuteCommand` RPC service.
5. These RTP clients are used to request the command (from step #2) execution on remote target nodes.
6. The command is delivered to the `435.ExecuteCommand` RPC server on target node.
7. The target node confirms the command execution and switches to "update" mode.
The node also remembers the path to the firmware file and the source node id from where the command had arrived -
this information will be used during the next "downloading" stage.
8. The confirmation response is passed back to the `ExecCmdService`. This step will fail with "timeout" if the node is not responding in time.
9. The `ExecCmdService` sends the response back to the `NodeCommandClient`, which collects the results for all nodes.
10. Once we've got responses from all nodes, the `ExecCmdService` sends the channel completion.
11. The `User` receives the overall result of the command execution, which is a map of node ids to their statuses or failures.
Note that even for not responding nodes there will be `Timedout` failure in the map (see step #8).

## Downloading the firmware file

```mermaid
sequenceDiagram
    autonumber
    participant FileProvider
    participant FileSystem as Root directories<br/>on the OS file system
    participant CyFileReadServer as Cyphal<br/>`408.Read.1.1`<br/>RPC server
    actor NodeX as NodeX<br/><<cyphal node>>
    
    activate NodeX
    par in parallel on each node
        loop repeat until EOF
            NodeX --)+ CyFileReadServer: uavcan::file::408.Read.<br/>Request_1_1{path, offset}
            deactivate NodeX
            Note right of NodeX: The node<br/>is reading the file chunk<br/>from the daemon node.
            CyFileReadServer ->>+ FileProvider: read(path, offset)
            FileProvider ->> FileProvider: findFirstValidFile(path)
            FileProvider ->> FileSystem: read(file, offset, size)
            FileProvider ->>- CyFileReadServer: file data chunk
            CyFileReadServer --)- NodeX: uavcan::file::408.Read.<br/>Response_1_1{data}
            activate NodeX
            Note left of NodeX: The node stores (in flash) the file chunk,<br/>moves the offset and repeats. 
        end
        Note right of NodeX: Download has completed.<br/>Verify and apply the firmware update.<br/>Restart the node.
    end
    deactivate NodeX

    box Daemon process<br/>《cyphal node》
        participant FileProvider
        participant FileSystem
        participant CyFileReadServer
    end
    box Cyphal Network Nodes
        actor NodeX
    end
```
Here are descriptions for arrows in the diagram:
1. A node in "update" mode sends a request to the `408.Read` RPC server on the daemon node to read a chunk of the firmware file.
2. The `408.Read` RPC server forwards the request to the `FileProvider`, which is responsible for reading files from the file system.
3. The `FileProvider` tries to find the requested file in one of the "root" directories.
4. Once the file is found, the `FileProvider` opens and reads the requested chunk of data from the file system.
5. The `FileProvider` sends the data chunk back to the `408.Read` RPC server as a response.
6. The RPC server posts the data chunk back to the node, which stores it in flash memory,
advances the offset, and repeats the process until the end of the file (EOF) is detected.

Once downloading is successfully completed, the node verifies the firmware file and applies the update.
After that, the node restarts and runs the new firmware.
