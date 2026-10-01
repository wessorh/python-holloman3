#!/usr/bin/env bash
# Generate omega_pb2.py + omega_pb2_grpc.py from the omega-scan proto so the
# watch_server example can import the OmegaScan gRPC stub.
#
# Requires: pip install grpcio-tools
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Path to the omega-scan proto. Override with OMEGA_PROTO=...
PROTO="${OMEGA_PROTO:-$HOME/work/ai/omega-scan/api/omega/v1/omega.proto}"

if [[ ! -f "$PROTO" ]]; then
  echo "error: proto not found at $PROTO" >&2
  echo "  set OMEGA_PROTO=/path/to/omega.proto" >&2
  exit 1
fi

python3 -m grpc_tools.protoc \
  -I "$(dirname "$PROTO")" \
  --python_out="$SCRIPT_DIR" \
  --grpc_python_out="$SCRIPT_DIR" \
  "$PROTO"

echo "generated omega_pb2.py + omega_pb2_grpc.py in $SCRIPT_DIR"
