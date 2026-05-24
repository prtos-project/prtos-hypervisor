import pytest
import ctypes
import struct
import sys
import os

# ---------------------------------------------------------------------------
# Minimal pure-Python simulation of the vulnerable kconfig symbol value
# assignment logic.  The simulation mirrors the C pattern:
#
#   size  = strlen(newval) + 1          (correct allocation)
#   val   = malloc(size)
#   strcpy(val, newval)                 (copy – safe only when size is right)
#
# We also model the *buggy* variant where size is computed from a *different*
# (shorter) string than the one actually copied, which is the class of bug
# described in the vulnerability report.
# ---------------------------------------------------------------------------

MAX_SYMBOL_VALUE_LEN = 4096  # reasonable upper bound for a kconfig value


class SymbolValueBuffer:
    """Simulates the kconfig heap buffer for a symbol value."""

    def __init__(self, allocated_size: int):
        if allocated_size <= 0:
            raise ValueError("allocated_size must be positive")
        self._buf = bytearray(allocated_size)
        self._allocated = allocated_size
        self._written = 0

    def safe_assign(self, newval: str) -> bool:
        """
        Invariant-safe assignment: reject or truncate values that would
        overflow the allocated buffer.  Returns True on success, False if
        the value was rejected.
        """
        encoded = newval.encode("utf-8", errors="replace")
        # Must fit including the NUL terminator
        if len(encoded) + 1 > self._allocated:
            return False  # reject – no overflow
        self._buf[: len(encoded)] = encoded
        self._buf[len(encoded)] = 0
        self._written = len(encoded)
        return True

    def buggy_assign(self, size_hint: int, newval: str) -> bool:
        """
        Simulates the buggy C pattern: buffer was allocated with `size_hint`
        bytes but we attempt to copy `newval` unconditionally (like strcpy).
        Returns True only when the copy would NOT overflow; raises
        AssertionError (caught by the test) if it would.
        """
        encoded = newval.encode("utf-8", errors="replace")
        required = len(encoded) + 1  # +1 for NUL
        # Detect the overflow that strcpy would cause
        if required > self._allocated:
            raise BufferError(
                f"Buffer overflow detected: need {required} bytes, "
                f"allocated {self._allocated} bytes"
            )
        self._buf[: len(encoded)] = encoded
        self._buf[len(encoded)] = 0
        self._written = len(encoded)
        return True

    @property
    def value(self) -> str:
        return self._buf[: self._written].decode("utf-8", errors="replace")

    @property
    def allocated(self) -> int:
        return self._allocated


def kconfig_sym_set_value(newval: str) -> str:
    """
    Reference implementation of the *correct* kconfig symbol value setter.
    Allocates exactly len(newval)+1 bytes and copies the value.
    Returns the stored value string.
    """
    if not isinstance(newval, str):
        raise TypeError("newval must be a str")

    encoded = newval.encode("utf-8", errors="replace")
    size = len(encoded) + 1  # mirrors: size = strlen(newval) + 1

    if size > MAX_SYMBOL_VALUE_LEN:
        # Reject oversized input – a hardened implementation must do this
        raise ValueError(f"Symbol value too long: {len(encoded)} bytes")

    buf = SymbolValueBuffer(size)
    ok = buf.safe_assign(newval)
    assert ok, "safe_assign must succeed when size == len(newval)+1"
    return buf.value


# ---------------------------------------------------------------------------
# Attack payloads
# ---------------------------------------------------------------------------

_BASE = "y"  # typical kconfig value

PAYLOADS = [
    # 2× the typical small buffer (8 bytes)
    "A" * 16,
    # 10× typical
    "A" * 80,
    # Exactly at the limit
    "B" * (MAX_SYMBOL_VALUE_LEN - 1),
    # One byte over the limit
    "C" * MAX_SYMBOL_VALUE_LEN,
    # 2× the limit
    "D" * (MAX_SYMBOL_VALUE_LEN * 2),
    # 10× the limit
    "E" * (MAX_SYMBOL_VALUE_LEN * 10),
    # Null bytes embedded (binary-style attack)
    "F\x00" * 100,
    # Unicode multi-byte characters that expand when encoded
    "\u00e9" * 500,   # 2-byte UTF-8 each → 1000 bytes
    "\u4e2d" * 500,   # 3-byte UTF-8 each → 1500 bytes
    "\U0001f600" * 300,  # 4-byte UTF-8 each → 1200 bytes
    # Format-string-style payloads (irrelevant in Python but realistic)
    "%s%s%s%s%s%s%s%s%s%s" * 50,
    # Path traversal / shell injection mixed with length
    "../../../etc/passwd" + "X" * 200,
    # Very long repeated pattern
    "kconfig_overflow_" * 300,
    # All-zero string (NUL-heavy)
    "\x00" * 256,
    # Mixed printable + non-printable
    "".join(chr(i % 256) for i in range(512)),
    # Exactly MAX_SYMBOL_VALUE_LEN - 1 (boundary, should succeed)
    "G" * (MAX_SYMBOL_VALUE_LEN - 1),
    # Empty string (edge case)
    "",
    # Single character
    "y",
    # Whitespace flood
    " " * 5000,
    # Newline / carriage-return injection
    "value\nmalicious=1\n" * 100,
]


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("payload", PAYLOADS)
def test_buffer_read_never_exceeds_declared_length(payload):
    """
    Invariant: Buffer reads/writes never exceed the declared (allocated) length.

    For every input string, the symbol value setter must either:
      (a) successfully store the value within a correctly-sized buffer, OR
      (b) reject the value with an exception — never silently overflow.

    The returned value (when accepted) must fit within MAX_SYMBOL_VALUE_LEN
    and the internal buffer must not have been written beyond its allocation.
    """
    encoded_len = len(payload.encode("utf-8", errors="replace"))

    if encoded_len >= MAX_SYMBOL_VALUE_LEN:
        # Oversized input MUST be rejected
        with pytest.raises((ValueError, BufferError, OverflowError)):
            kconfig_sym_set_value(payload)
    else:
        # Input fits – must be stored correctly without overflow
        result = kconfig_sym_set_value(payload)
        result_encoded = result.encode("utf-8", errors="replace")

        # The stored value must not be longer than what was allocated
        assert len(result_encoded) <= encoded_len, (
            f"Stored value is longer than input: "
            f"{len(result_encoded)} > {encoded_len}"
        )
        # The stored value must fit within the hard limit
        assert len(result_encoded) < MAX_SYMBOL_VALUE_LEN, (
            f"Stored value exceeds MAX_SYMBOL_VALUE_LEN: {len(result_encoded)}"
        )


@pytest.mark.parametrize("payload", PAYLOADS)
def test_buggy_assign_raises_on_overflow(payload):
    """
    Invariant: The buggy strcpy-style assignment must raise BufferError
    whenever the payload is larger than the allocated buffer, proving that
    any such overflow is detectable and must be guarded against.
    """
    encoded = payload.encode("utf-8", errors="replace")
    required = len(encoded) + 1  # bytes needed including NUL

    # Simulate the bug: allocate a buffer that is HALF the required size
    # (models the race / logic error in size computation)
    if required < 2:
        pytest.skip("Payload too short to demonstrate overflow with half-size buffer")

    half_size = max(1, required // 2)
    buf = SymbolValueBuffer(half_size)

    # The buggy assignment MUST raise BufferError – never silently overflow
    with pytest.raises(BufferError):
        buf.buggy_assign(half_size, payload)


@pytest.mark.parametrize("payload", PAYLOADS)
def test_allocated_size_equals_strlen_plus_one(payload):
    """
    Invariant: The allocated buffer size must equal strlen(newval) + 1.
    Any deviation (under-allocation) is a potential overflow source.
    """
    encoded = payload.encode("utf-8", errors="replace")
    required_size = len(encoded) + 1

    if required_size > MAX_SYMBOL_VALUE_LEN:
        pytest.skip("Payload exceeds max length; rejection tested elsewhere")

    # Correct allocation
    buf = SymbolValueBuffer(required_size)
    assert buf.allocated == required_size, (
        f"Buffer allocated {buf.allocated} bytes but need {required_size}"
    )

    ok = buf.safe_assign(payload)
    assert ok, "safe_assign must succeed with correctly sized buffer"

    # Verify no byte beyond the allocation was touched
    stored = buf.value.encode("utf-8", errors="replace")
    assert len(stored) + 1 <= buf.allocated, (
        f"Written data ({len(stored)+1} bytes with NUL) exceeds "
        f"allocated size ({buf.allocated})"
    )


@pytest.mark.parametrize("overflow_factor", [2, 5, 10, 100])
@pytest.mark.parametrize("base_size", [8, 64, 256, 1024])
def test_oversized_payload_rejected_by_factor(overflow_factor, base_size):
    """
    Invariant: Payloads that are N× larger than the allocated buffer must
    always be rejected, regardless of the overflow factor.
    """
    payload = "X" * (base_size * overflow_factor)
    encoded = payload.encode("utf-8", errors="replace")
    required = len(encoded) + 1

    # Allocate only base_size bytes (simulating under-allocation)
    buf = SymbolValueBuffer(base_size)

    if required > buf.allocated:
        with pytest.raises(BufferError):
            buf.buggy_assign(base_size, payload)
    else:
        # If somehow it fits, safe_assign must succeed without overflow
        ok = buf.safe_assign(payload)
        assert ok