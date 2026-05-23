#!/bin/bash
# Install udev rules for the Hiwonder ROS Robot Controller (STM32 co-processor).
# This creates the /dev/rrc symlink required by the Board serial driver.
#
# Run once after cloning / building the workspace:
#   sudo bash etc/udev/install_udev_rules.sh

set -e

RULES_FILE="$(dirname "$0")/99-ttyACM0.rules"
DEST="/etc/udev/rules.d/99-ttyACM0.rules"

echo "Installing udev rule: $RULES_FILE -> $DEST"
sudo cp "$RULES_FILE" "$DEST"

echo "Reloading udev rules..."
sudo udevadm control --reload-rules
sudo udevadm trigger

echo ""
echo "Done. Reconnect the ROS Robot Controller USB cable if it was already plugged in."
echo "The STM32 board will appear as /dev/rrc (symlink to /dev/ttyACM0)."
