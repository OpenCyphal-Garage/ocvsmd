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
  cmake --preset OCVSMD-Linux
  cmake --build --preset OCVSMD-Linux-Debug
  ```
  ###### Release
  ```bash
  cmake --preset OCVSMD-Linux
  cmake --build --preset OCVSMD-Linux-Release
  ```

### Installing

- Installing the Daemon Binary:
  ###### Debug
  ```bash
  sudo cp build/bin/Debug/ocvsmd /usr/local/bin/ocvsmd
  sudo cp build/bin/Debug/ocvsmd-cli /usr/local/bin/ocvsmd-cli
  ```
  ###### Release
  ```bash
  sudo cp build/bin/Release/ocvsmd /usr/local/bin/ocvsmd
  sudo cp build/bin/Release/ocvsmd-cli /usr/local/bin/ocvsmd-cli
  ```
- Installing the Init Script:
  ```bash
  sudo cp init.d/ocvsmd /etc/init.d/ocvsmd
  sudo chmod +x /etc/init.d/ocvsmd
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
- View logs:
  ```bash
  sudo grep -r "ocvsmd\[" /var/log/syslog
  ```
