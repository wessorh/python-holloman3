"""SMTP header normalization for clustering.

A faithful Python port of ``smtp-feed/internal/parser/normalize.go`` and the
header/body split in ``smtp-feed/internal/parser/parser.go``. It operates on
bytes throughout so the resulting fingerprints match the Go pipeline exactly.

The raw header block is dominated by per-message-unique headers (Message-ID,
Received, Date, DKIM-Signature, …), so its fingerprint barely clusters.
Dropping those and reducing From/Reply-To/Sender to their @domain yields a
"campaign envelope" signature that clusters by sender infrastructure.
"""

DROPPED_HEADERS = frozenset([
    b"message-id",
    b"received",
    b"date",
    b"dkim-signature",
    b"return-path",
    b"authentication-results",
    b"arc-seal",
    b"arc-message-signature",
    b"arc-authentication-results",
    b"x-received",
    b"x-originating-ip",
    b"x-originating-host",
    b"delivered-to",
    b"subject",
])

DOMAIN_HEADERS = frozenset([
    b"from",
    b"reply-to",
    b"sender",
])


def split_message(msg):
    """Split an RFC 5322 message into (headers, body) at the first blank line.

    Uses ``\\r\\n\\r\\n`` first, falling back to ``\\n\\n``.
    """
    idx = msg.find(b"\r\n\r\n")
    if idx >= 0:
        return msg[:idx], msg[idx + 4:]
    idx = msg.find(b"\n\n")
    if idx >= 0:
        return msg[:idx], msg[idx + 2:]
    return msg, b""


def normalize_headers(headers):
    """Return a canonical, unfolded header block suitable for clustering.

    If the normalized result is empty (e.g. only dropped headers), the raw
    headers are returned unchanged.
    """
    out = []
    for field in _unfold_header_fields(headers):
        name, value = _split_header_field(field)
        if name is None:
            continue
        lower = name.lower()
        if lower in DROPPED_HEADERS:
            continue
        if lower in DOMAIN_HEADERS:
            domains = _extract_domains(value)
            if not domains:
                continue
            out.append(lower + b": " + b",".join(domains) + b"\n")
        elif lower == b"content-type":
            out.append(b"content-type: " + _media_type(value) + b"\n")
        else:
            out.append(lower + b": " + value + b"\n")
    if not out:
        return headers
    return b"".join(out)


def _unfold_header_fields(headers):
    """Unfold RFC 5322 folded headers, returning one field per element."""
    fields = []
    cur = None

    def flush():
        nonlocal cur
        if cur is not None:
            cur = cur.strip()
            if cur:
                fields.append(cur)
        cur = None

    while headers:
        n = len(headers)
        i = 0
        while i < n and headers[i] != 0x0A:  # b"\n"
            i += 1
        line = headers[:i].rstrip(b"\r")
        headers = headers[i + 1:] if i < n else b""

        if line[:1] in (b" ", b"\t"):
            if cur is None:
                cur = line
            else:
                cur = cur + b" " + line.strip()
            continue
        flush()
        cur = line
    flush()
    return fields


def _split_header_field(field):
    """Split ``b"Name: value"`` into ``(name, value)``, or ``(None, None)``."""
    colon = field.find(b":")
    if colon <= 0:
        return None, None
    return field[:colon].strip(), field[colon + 1:].strip()


def _extract_domains(value):
    """Return the unique, lowercased @domains in an address header value."""
    domains = []
    seen = set()
    n = len(value)
    i = 0
    while i < n:
        if value[i] != 0x40:  # b"@"
            i += 1
            continue
        j = i + 1
        while j < n and _is_domain_byte(value[j]):
            j += 1
        if j == i + 1:
            i += 1
            continue
        d = value[i + 1:j].lower()
        if d.endswith(b"."):
            d = d[:-1]
        if d not in seen:
            seen.add(d)
            domains.append(d)
        i = j
    return domains


def _is_domain_byte(c):
    return (
        (0x61 <= c <= 0x7A)  # a-z
        or (0x41 <= c <= 0x5A)  # A-Z
        or (0x30 <= c <= 0x39)  # 0-9
        or c == 0x2D  # '-'
        or c == 0x2E  # '.'
    )


def _media_type(value):
    """Return the part of a Content-Type value before the first ';'."""
    i = value.find(b";")
    if i >= 0:
        return value[:i].strip()
    return value.strip()
