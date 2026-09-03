#!/bin/bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"
while IFS="=" read -r key val; do
  [[ "$key" =~ ^# ]] && continue
  [[ -z "$key" ]] && continue
  export "$key"="$val"
done < "$DIR/.env"
exec "$DIR/belya" --telegram
