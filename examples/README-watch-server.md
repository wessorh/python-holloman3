# watch_server.py — holloman3 + omega-scan file watcher example

A minimal end-to-end example showing how to use the `holloman3` Python library
together with omega-scan's gRPC threat-lookup API.

It watches a directory for files that are closed after being written
(`CLOSE_WRITE`), fingerprints each one with holloman3, hashes it (SHA-1 /
SHA-256), asks omega-scan to label it, and finally calls a **`noop()`**
callback — the one function you implement to persist the result.

## Files

| File | Purpose |
|---|---|
| `watch_server.py` | the example server |
| `watch_server.toml.example` | config (copy to `watch_server.toml`) |
| `generate_grpc.sh` | generates `omega_pb2.py` / `omega_pb2_grpc.py` |
| `requirements.txt` | Python dependencies |

## Setup

```bash
# 1. install the holloman3 binding (if not already)
cd .. && pip install -e .

# 2. install this example's dependencies
cd examples
pip install -r requirements.txt

# 3. generate the gRPC bindings from the omega-scan proto
pip install grpcio-tools        # one-time
./generate_grpc.sh              # writes omega_pb2.py + omega_pb2_grpc.py

# 4. configure
cp watch_server.toml.example watch_server.toml
#    edit watch_server.toml -> set [watch].directory and [server].address
```

## Run

```bash
python examples/watch_server.py examples/watch_server.toml
```

Drop a file into the watched directory; it will be fingerprinted and looked up
on the next `CLOSE_WRITE`, e.g.:

```
watching /tmp/inbox (order=13, grpc=api.dewile.net:8443)
/tmp/inbox/sample.exe: h.00286058000750550000040500000000 [malicious] score=87 family=trojan.eshopscam category=code-injection
```

## Implementing `noop()`

The example's `noop(path, cluster_id, resp)` is a no-op on purpose. Replace its
body with whatever labeling you need:

```python
def noop(path, cluster_id, resp):
    # e.g. write a sidecar
    with open(path + ".meta", "w") as f:
        json.dump({
            "cluster_id": cluster_id,
            "family": resp.family,
            "category": resp.category,
            "threat_score": resp.threat_score,
            "malicious": resp.malicious,
        }, f)
```

`resp` is an `omega_pb2.LookupResponse`; see
`~/work/ai/omega-scan/api/omega/v1/omega.proto` for the full field list.

## Notes

- **Order 13** is the default (`[holloman].order = 13`), covering files up to
  ~64 MB. `holloman3.init(order)` must be called before `fingerprint()`.
- The gRPC bindings are not committed; regenerate them with `./generate_grpc.sh`
  (override the proto path with `OMEGA_PROTO=...`).
- `insecure = false` is the default (TLS); set `insecure = true` only for a
  local/self-signed endpoint (see omega-scan docs for mTLS options).
