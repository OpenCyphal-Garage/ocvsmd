#!/usr/bin/env bash

# +----------------------------------------------------------+
# | BASH : Modifying Shell Behaviour
# |    (https://www.gnu.org/software/bash/manual)
# +----------------------------------------------------------+
# Treat unset variables and parameters other than the special
# parameters ‘@’ or ‘*’ as an error when performing parameter
# expansion. An error message will be written to the standard
# error, and a non-interactive shell will exit.
set -o nounset

# Exit immediately if a pipeline returns a non-zero status.
set -o errexit

# If set, the return value of a pipeline is the value of the
# last (rightmost) command to exit with a non-zero status, or
# zero if all commands in the pipeline exit successfully.
set -o pipefail

if [[ $# -ne 1 ]]; then
  BUILD_TYPE="Debug"
else
  BUILD_TYPE=$1
fi

if [[ "$BUILD_TYPE" != "Release" && "$BUILD_TYPE" != "Debug" ]]; then
  echo "Invalid argument: $BUILD_TYPE"
  echo "Usage: $0 {Release|Debug}"
  exit 1
fi

set +e
if [[ -L /usr/local/bin/ocvsmd ]]; then
  unlink /usr/local/bin/ocvsmd
fi

if [[ -L /usr/local/bin/ocvsmd-cli ]]; then
  unlink /usr/local/bin/ocvsmd-cli
fi

if [[ -L /etc/init.d/ocvsmd ]]; then
  /etc/init.d/ocvsmd stop
  unlink /etc/init.d/ocvsmd
fi

if [[ -L /etc/ocvsmd/ocvsmd.toml ]]; then
  unlink /etc/ocvsmd/ocvsmd.toml
fi

ln -s /repo/build/bin/$BUILD_TYPE/ocvsmd /usr/local/bin/ocvsmd
ln -s /repo/build/bin/$BUILD_TYPE/ocvsmd-cli /usr/local/bin/ocvsmd-cli

ln -s /repo/init.d/ocvsmd /etc/init.d/ocvsmd
chmod +x /etc/init.d/ocvsmd
mkdir -p /etc/ocvsmd
ln -s /repo/init.d/ocvsmd.toml /etc/ocvsmd/ocvsmd.toml

set -e
echo "Links should be setup. Starting..."

/etc/init.d/ocvsmd start SPDLOG_LEVEL=trace SPDLOG_FLUSH_LEVEL=trace

echo "Linked $BUILD_TYPE build artifacts to container's system. Use /etc/init.d/ocvsmd [start|stop|status|restart] to control daemon"
echo "Starting log tail of /var/log/ocvsmd.log... "

tail -f /var/log/ocvsmd.log
