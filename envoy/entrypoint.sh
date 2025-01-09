#!/usr/bin/env bash
if [ -z "$YARILO_ADDRESS" ]; then
	echo "No YARILO_ADDRESS specified, defaulting to 0.0.0.0"
	export YARILO_ADDRESS="0.0.0.0"
fi

sed "s/address: 0\.0\.0\.0$/address: $YARILO_ADDRESS/" /app/envoy-original.yaml >/app/envoy.yaml
/app/bin/envoy $*
