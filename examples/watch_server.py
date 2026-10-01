#!/usr/bin/env python3
"""Example holloman3 + omega-scan file watcher.

Watches a directory (from a TOML config) for files that are closed after
being written (CLOSE_WRITE). For each new file it:

  1. computes the holloman3 cluster id (perceptual fingerprint),
  2. computes SHA-1 and SHA-256,
  3. calls omega-scan's gRPC ``Lookup`` to fetch threat metadata,
  4. prints a one-line summary and hands the result to :func:`noop` — the
     single callback the end user implements to persist/label the file.

Usage:
    pip install -r requirements.txt     # grpcio, protobuf, watchdog
    ./generate_grpc.sh                  # builds omega_pb2.py / omega_pb2_grpc.py
    python examples/watch_server.py examples/watch_server.toml
"""

import hashlib
import sys
import tomllib

import grpc
from watchdog.events import FileSystemEventHandler
from watchdog.observers import Observer

import holloman3

import omega_pb2
import omega_pb2_grpc


# ── end-user hook ─────────────────────────────────────────────────────────────

def noop(path, cluster_id, resp):
    """Label/persist ``path`` using the metadata in ``resp``.

    This is intentionally a no-op so the example runs out of the box.
    Implement whatever labeling you need here — write a sidecar JSON, rename
    the file, insert into a database, enqueue an upload, etc.

    ``resp`` is an ``omega_pb2.LookupResponse`` with fields: malicious,
    threat_score, severity, family, category, tags, av_detections, av_total,
    first_seen, last_seen, cluster_size, nearest_neighbors.
    """
    # TODO(user): implement your labeling/persistence here.
    return None


# ── hashing / fingerprinting helpers ─────────────────────────────────────────

def _file_hash(path, algo):
    h = algo()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sha1_hex(path):
    return _file_hash(path, hashlib.sha1)


def sha256_hex(path):
    return _file_hash(path, hashlib.sha256)


def fingerprint_file(path):
    with open(path, "rb") as f:
        return holloman3.fingerprint(f.read())


# ── watchdog event handler ────────────────────────────────────────────────────

class Handler(FileSystemEventHandler):
    """Process files on close (CLOSE_WRITE) — the "file is complete" signal."""

    def __init__(self, cfg, stub):
        self.lookup = cfg.get("lookup", {})
        self.stub = stub

    def on_closed(self, event):
        # on_closed fires for IN_CLOSE_WRITE (and IN_CLOSE_NOWRITE). Only a
        # file that was opened for writing and then closed is safe to read.
        if event.is_directory:
            return
        self.process(event.src_path)

    def process(self, path):
        try:
            cluster_id = fingerprint_file(path)
        except Exception as exc:  # noqa: BLE001 — report and move on
            print(f"ERR fingerprint {path}: {exc}", file=sys.stderr)
            return

        try:
            resp = self.stub.Lookup(omega_pb2.LookupRequest(
                cluster_id=cluster_id,
                sha1=sha1_hex(path),
                sha256=sha256_hex(path),
                artifact_type=omega_pb2.ARTIFACT_TYPE_FILE,
                client_id=self.lookup.get("client_id", ""),
                api_token=self.lookup.get("api_token", ""),
            ))
        except grpc.RpcError as exc:
            print(f"ERR lookup {path}: {exc}", file=sys.stderr)
            return

        verdict = "malicious" if resp.malicious else "benign"
        print(f"{path}: {cluster_id} [{verdict}] "
              f"score={resp.threat_score} family={resp.family or '-'} "
              f"category={resp.category or '-'}", flush=True)
        noop(path, cluster_id, resp)


# ── main ──────────────────────────────────────────────────────────────────────

def main():
    config_path = sys.argv[1] if len(sys.argv) > 1 else "watch_server.toml"
    with open(config_path, "rb") as f:
        cfg = tomllib.load(f)

    watch_dir = cfg["watch"]["directory"]
    order = cfg.get("holloman", {}).get("order", 13)
    server = cfg.get("server", {})
    address = server.get("address", "localhost:8443")

    holloman3.init(order)

    if server.get("insecure", True):
        channel = grpc.insecure_channel(address)
    else:
        channel = grpc.secure_channel(address, grpc.ssl_channel_credentials())
    stub = omega_pb2_grpc.OmegaScanStub(channel)

    observer = Observer()
    observer.schedule(Handler(cfg, stub), watch_dir, recursive=True)
    observer.start()
    print(f"watching {watch_dir} (order={order}, grpc={address})", file=sys.stderr)

    try:
        observer.join()
    except KeyboardInterrupt:
        pass
    finally:
        observer.stop()
        observer.join()
        holloman3.cleanup()


if __name__ == "__main__":
    main()
