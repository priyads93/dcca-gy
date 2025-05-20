#!/bin/bash
echo "# Configuring freeDiameter..."
sed -i.bak -Ee "s/\\\$diameterID/${diameterID:-peer1}/g" \
 -e "s/\\\$domainName/${domainName:-localdomain}/g" \
 -e /etc/freeDiameter/freeDiameter.conf \
 || echo "Failed !"


export LD_LIBRARY_PATH=/usr/local/lib

echo "# Starting the daemon..."
/usr/local/bin/freeDiameterd -dd ${params} || echo "Daemon Failed to start!"