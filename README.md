# OCVSMD - Open Cyphal Vehicle System Management Daemon

### Build
- Change directory to the project root; init submodules:
  ```
  cd ocvsmd
  git submodule update --init --recursive
  ```
Then one of the two presets depending on your system:
- `Demo-Linux` – Linux distros like Ubuntu.
- `Demo-BSD` – BSD based like MacOS.
  ###### Debug
  ```bash
  cmake --preset OCVSMD-Linux && \
  cmake --build --preset OCVSMD-Linux-Debug
  ```
  ###### Release
  ```bash
  cmake --preset OCVSMD-Linux && \
  cmake --build --preset OCVSMD-Linux-Release
  ```

### Installing

- Installing the Daemon Binary:
  ###### Debug
  ```bash
  sudo cp build/bin/Debug/ocvsmd /usr/local/bin/ocvsmd && \
  sudo cp build/bin/Debug/ocvsmd-cli /usr/local/bin/ocvsmd-cli
  ```
  ###### Release
  ```bash
  sudo cp build/bin/Release/ocvsmd /usr/local/bin/ocvsmd && \
  sudo cp build/bin/Release/ocvsmd-cli /usr/local/bin/ocvsmd-cli
  ```
- Installing the Init Script and Config file:
  ```bash
  sudo cp init.d/ocvsmd /etc/init.d/ocvsmd && \
  sudo chmod +x /etc/init.d/ocvsmd && \
  sudo mkdir -p /etc/ocvsmd && \
  sudo cp init.d/ocvsmd.toml /etc/ocvsmd/ocvsmd.toml
  ```

- Enabling at Startup if needed (on SysV-based systems):
  ```
  sudo update-rc.d ocvsmd defaults
  ```

### Usage
- Control the daemon using the following commands:
  ###### Start
  ```bash
  sudo /etc/init.d/ocvsmd start
  ```
  ###### Status
  ```bash
  sudo /etc/init.d/ocvsmd status
  ```
  ###### Restart
  ```bash
  sudo /etc/init.d/ocvsmd restart
  ```
  ###### Stop
  ```bash
  sudo /etc/init.d/ocvsmd stop
  ```
### Logging

#### View logs:
  - Syslog: 
    ```bash
    sudo grep -r "ocvsmd\[" /var/log/syslog
    ```
  - Log files:
    ```bash
    cat /var/log/ocvsmd.log
    ```
#### Manipulate log levels:

  Append `SPDLOG_LEVEL` and/or `SPDLOG_FLUSH_LEVEL` to the daemon command in the init script
  to enable more verbose logging level (`trace` or `debug`).
  Default level is `info`. More severe levels are: `warn`, `error` and `critical`.
  `off` level disables the logging.
  
By default, the log files are not immediately flushed to disk (at `off` level).
To enable flushing, set `SPDLOG_FLUSH_LEVEL` to a required default (or per component) level.

  - Example to set default level:
      ```bash
      sudo /etc/init.d/ocvsmd start SPDLOG_LEVEL=trace SPDLOG_FLUSH_LEVEL=trace
      ```
  - Example to set default and per component level (comma separated pairs):
      ```bash
      sudo /etc/init.d/ocvsmd restart SPDLOG_LEVEL=debug,ipc=off SPDLOG_FLUSH_LEVEL=warn,engine=debug
      ```

### vscode

Using vscode, relaunch this workspace using the appropriate devcontainer provided. Once running in the toolshed container build, using cmake, and then do "Tasks: Run Task -> Create OCVSMD Terminals". This will launch four terminals all running in the container:

- **Daemon** – Creates symbolic links to the build artifacts under the appropriate places in /etc, then starts the daemon, then tails the daemon logs.
- **Yakut** – Starts a Yakut UDP monitoring session.
- **Client** – Tails "ocvsmd-cli.log" in the workspace root (touches first if this doesn't exist). This allows you to use the **shell** to run the cli in the workspace root and see the logs in this terminal.
- **Bash shell** – login shell starting in the workspace root.
