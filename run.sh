#!/bin/bash
cd /opt/charness
while IFS="=" read -r key val; do
  [[ "$key" =~ ^# ]] && continue
  [[ -z "$key" ]] && continue
  export "$key"="$val"
done < /opt/charness/.env
exec /opt/charness/c_agent_system --telegram
