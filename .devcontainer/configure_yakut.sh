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

yakut compile --output /root/types /root/public_regulated_data_types/uavcan
echo "export YAKUT_PATH=\"/root/types\"" >> ~/.bashrc

echo "Enabled Yakut with public_regulated_data_types. For example do: yakut -i \"UDP('127.0.0.1', 27)\" monitor"
