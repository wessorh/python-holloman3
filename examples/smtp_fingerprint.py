#!/usr/bin/env python3
"""Compute the three smtp-feed fingerprints for a single ``.eml`` message.

Usage:
    python examples/smtp_fingerprint.py <file.eml>

Prints one holloman3 fingerprint per line on stdout, matching the smtp-feed
pipeline: normalized header (fp_headers_norm), body (fp_body), and the entire
raw message (fp_message).
"""

import sys

import holloman3
from holloman3.normalize import split_message, normalize_headers


def main():
    if len(sys.argv) != 2:
        sys.stderr.write("usage: smtp_fingerprint.py <file.eml>\n")
        sys.exit(2)

    with open(sys.argv[1], "rb") as f:
        data = f.read()

    headers, body = split_message(data)
    normalized = normalize_headers(headers)

    holloman3.init(13)
    try:
        print("header_norm: %s" % _fingerprint_or_empty(normalized))
        print("body:        %s" % _fingerprint_or_empty(body))
        print("message:     %s" % _fingerprint_or_empty(data))
    finally:
        holloman3.cleanup()


def _fingerprint_or_empty(part):
    return holloman3.fingerprint(part) if part else "(empty)"


if __name__ == "__main__":
    main()
