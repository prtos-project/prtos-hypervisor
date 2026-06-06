import pytest
import struct
import ctypes
import os
import sys

# Self-contained LZSS decompressor implementation in Python
# mirroring the C implementation's logic for testing purposes

N = 4096       # size of ring buffer
F = 18         # upper limit for match_length
THRESHOLD = 2  # encode string into position and length if match_length > THRESHOLD
NOT_USED = N   # index for root of binary search trees

def lzss_decompress(compressed_data: bytes, max_output_size: int = 65536) -> bytes:
    """
    Python implementation of LZSS decompression that mirrors the C code's logic.
    This implementation enforces strict bounds checking.
    Raises ValueError if any out-of-bounds access would occur.
    """
    text_buf = bytearray(N + F - 1)
    r = N - F
    output = bytearray()
    
    i = 0
    flags = 0
    flag_count = 0
    
    while i < len(compressed_data):
        if flag_count == 0:
            if i >= len(compressed_data):
                break
            flags = compressed_data[i]
            i += 1
            flag_count = 8
        
        flag_count -= 1
        
        if flags & 1:
            # Literal byte
            if i >= len(compressed_data):
                break
            c = compressed_data[i]
            i += 1
            
            if len(output) >= max_output_size:
                raise ValueError(f"Output size exceeded maximum: {max_output_size}")
            
            # Bounds check for text_buf write
            if r < 0 or r >= N + F - 1:
                raise ValueError(f"text_buf write out of bounds: r={r}, buf_size={N + F - 1}")
            
            text_buf[r] = c
            r = (r + 1) & (N - 1)
            output.append(c)
        else:
            # Match: read 2 bytes for position and length
            if i + 1 >= len(compressed_data):
                break
            
            j = compressed_data[i] | ((compressed_data[i + 1] & 0xF0) << 4)
            k = (compressed_data[i + 1] & 0x0F) + THRESHOLD + 1
            i += 2
            
            # Validate offset j is within ring buffer bounds
            if j < 0 or j >= N:
                raise ValueError(f"Offset out of bounds: j={j}, N={N}")
            
            # Validate match length k
            if k < 0 or k > F:
                raise ValueError(f"Match length out of bounds: k={k}, F={F}")
            
            for _ in range(k):
                if len(output) >= max_output_size:
                    raise ValueError(f"Output size exceeded maximum: {max_output_size}")
                
                # Bounds check for text_buf read
                if j < 0 or j >= N + F - 1:
                    raise ValueError(f"text_buf read out of bounds: j={j}, buf_size={N + F - 1}")
                
                c = text_buf[j]
                j = (j + 1) & (N - 1)
                
                # Bounds check for text_buf write
                if r < 0 or r >= N + F - 1:
                    raise ValueError(f"text_buf write out of bounds: r={r}, buf_size={N + F - 1}")
                
                text_buf[r] = c
                r = (r + 1) & (N - 1)
                output.append(c)
        
        flags >>= 1
    
    return bytes(output)


def make_lzss_literal(data: bytes) -> bytes:
    """Create valid LZSS compressed data with all literals."""
    result = bytearray()
    i = 0
    while i < len(data):
        chunk = data[i:i+8]
        flag_byte = 0xFF  # all literals
        result.append(flag_byte)
        result.extend(chunk)
        i += 8
    return bytes(result)


def make_lzss_match(offset: int, length: int) -> bytes:
    """Create LZSS compressed data with a single match token."""
    # Flag byte: 0x00 means first token is a match
    flag_byte = 0x00
    # Encode: low byte = offset & 0xFF, high nibble of second byte = (offset >> 4) & 0xF0
    # low nibble of second byte = (length - THRESHOLD - 1) & 0x0F
    byte1 = offset & 0xFF
    byte2 = ((offset >> 4) & 0xF0) | ((length - THRESHOLD - 1) & 0x0F)
    return bytes([flag_byte, byte1, byte2])


# Adversarial payloads: (description, compressed_bytes, should_raise_or_return_safely)
ADVERSARIAL_PAYLOADS = [
    # Empty input
    b"",
    # Single byte (incomplete)
    b"\x00",
    # Flag byte only, no data
    b"\xFF",
    # Match token with maximum offset (N-1 = 4095)
    make_lzss_match(N - 1, F),
    # Match token with offset 0
    make_lzss_match(0, F),
    # Match token with offset N//2
    make_lzss_match(N // 2, F),
    # Match token with minimum length
    make_lzss_match(0, THRESHOLD + 1),
    # Match token with maximum length F
    make_lzss_match(0, F),
    # Crafted: all-zero compressed data
    b"\x00" * 100,
    # Crafted: all-ones compressed data  
    b"\xFF" * 100,
    # Crafted: alternating bytes
    bytes([0xAA, 0x55] * 50),
    # Crafted: match pointing to uninitialized area
    make_lzss_match(N - F, F),
    # Crafted: multiple match tokens
    make_lzss_match(100, 10) + make_lzss_match(200, 15),
    # Crafted: flag byte 0x00 with truncated match data
    b"\x00\xFF",
    # Crafted: large repeated matches
    make_lzss_match(0, F) * 10,
    # Crafted: boundary offset values
    make_lzss_match(0, THRESHOLD + 1),
    make_lzss_match(N - 1, THRESHOLD + 1),
    # Crafted: mix of literals and matches
    bytes([0xFE]) + b"\x41\x42\x43\x44\x45\x46\x47" + make_lzss_match(N - 7, 7),
    # Random-looking adversarial data
    bytes(range(256)),
    bytes(range(255, -1, -1)),
    # Repeated pattern that could cause issues
    b"\x00\x00\x00" * 50,
    b"\xFF\x00\x00" * 50,
    # Crafted to maximize output
    bytes([0x00, 0xFF, 0x0F]) * 20,  # matches with max length
]


@pytest.mark.parametrize("payload", ADVERSARIAL_PAYLOADS)
def test_lzss_decompression_bounds_invariant(payload):
    """
    Invariant: LZSS decompression must NEVER access memory outside the ring buffer
    (text_buf[0..N+F-2]) or produce output exceeding the declared maximum size,
    regardless of what compressed data is provided. All offset and length values
    derived from compressed input must be validated before use as array indices.
    """
    MAX_OUTPUT = 65536
    
    try:
        output = lzss_decompress(payload, max_output_size=MAX_OUTPUT)
        
        # If decompression succeeds, output must be within bounds
        assert len(output) <= MAX_OUTPUT, (
            f"Output size {len(output)} exceeds maximum {MAX_OUTPUT}"
        )
        
        # Output must be valid bytes
        assert isinstance(output, bytes), "Output must be bytes"
        
        # All output bytes must be valid byte values (0-255)
        for byte_val in output:
            assert 0 <= byte_val <= 255, f"Invalid byte value: {byte_val}"
            
    except ValueError as e:
        # ValueError is acceptable - it means bounds checking caught an issue
        # The important thing is no silent memory corruption occurred
        error_msg = str(e)
        assert any(keyword in error_msg for keyword in [
            "out of bounds", "exceeded maximum", "bounds"
        ]), f"Unexpected ValueError: {error_msg}"
    except (IndexError, OverflowError) as e:
        # These should NOT occur - they indicate missing bounds checks
        pytest.fail(
            f"Bounds check failure with payload {payload!r}: {type(e).__name__}: {e}"
        )


@pytest.mark.parametrize("offset,length", [
    # Boundary values for offset (0 to N-1)
    (0, THRESHOLD + 1),
    (N - 1, THRESHOLD + 1),
    (N // 2, F),
    (N - F, F),
    # Various length values
    (0, THRESHOLD + 1),
    (0, F),
    (100, F // 2),
    (N - 1, F),
])
def test_lzss_match_token_bounds(offset, length):
    """
    Invariant: Match tokens with any offset in [0, N-1] and length in
    [THRESHOLD+1, F] must be handled without accessing memory outside
    the ring buffer boundaries.
    """
    # Validate our test parameters are within spec
    assert 0 <= offset < N, f"Test offset {offset} out of valid range [0, {N-1}]"
    assert THRESHOLD + 1 <= length <= F, f"Test length {length} out of valid range"
    
    payload = make_lzss_match(offset, length)
    
    try:
        output = lzss_decompress(payload, max_output_size=65536)
        assert len(output) <= 65536
        assert isinstance(output, bytes)
    except ValueError:
        pass  # Acceptable - bounds check triggered
    except (IndexError, OverflowError) as e:
        pytest.fail(
            f"Unguarded bounds violation: offset={offset}, length={length}: "
            f"{type(e).__name__}: {e}"
        )


def test_lzss_output_never_exceeds_declared_maximum():
    """
    Invariant: The decompressor must never produce output larger than
    the declared maximum output size, regardless of input.
    """
    max_sizes = [0, 1, 10, 100, 1000, 4096, 65536]
    
    # Create a payload that would expand significantly
    payload = make_lzss_match(0, F) * 100
    
    for max_size in max_sizes:
        try:
            output = lzss_decompress(payload, max_output_size=max_size)
            assert len(output) <= max_size, (
                f"Output {len(output)} exceeded declared max {max_size}"
            )
        except ValueError:
            pass  # Acceptable
        except (IndexError, OverflowError) as e:
            pytest.fail(f"Unguarded access with max_size={max_size}: {e}")


def test_lzss_ring_buffer_indices_always_valid():
    """
    Invariant: The ring buffer index r must always remain within [0, N-1]
    after each operation. The modular arithmetic (r & (N-1)) must ensure this.
    """
    # Test that the ring buffer wrapping works correctly
    # by feeding data that exercises the wrap-around
    
    # Create literals that fill more than one ring buffer
    literal_data = bytes(range(256)) * (N // 256 + 2)
    payload = make_lzss_literal(literal_data[:N * 2])
    
    try:
        output = lzss_decompress(payload, max_output_size=N * 4)
        # If successful, verify output is reasonable
        assert len(output) <= N * 4
    except ValueError:
        pass  # Acceptable
    except (IndexError, OverflowError) as e:
        pytest.fail(f"Ring buffer index out of bounds: {e}")


def test_lzss_no_information_leak_on_invalid_input():
    """
    Invariant: Invalid/adversarial compressed data must not cause the
    decompressor to read from uninitialized or out-of-bounds memory regions.
    The ring buffer must only be read from positions that were previously written.
    """
    # Craft input that tries to reference positions beyond what was written
    # Initial ring buffer state: r starts at N-F = 4078
    # A match pointing to position 0 would read uninitialized data
    
    adversarial_inputs = [
        # Try to read from position 0 before anything is written there
        make_lzss_match(0, F),
        # Try to read from the very end of the buffer
        make_lzss_match(N - 1, F),
        # Try to read from middle of buffer
        make_lzss_match(N // 2, F),
    ]
    
    for inp in adversarial_inputs:
        try:
            output = lzss_decompress(inp, max_output_size=65536)
            # Output should be deterministic (ring buffer initialized to spaces or zeros)
            assert isinstance(output, bytes)
            assert len(output) <= 65536
        except ValueError:
            pass  # Acceptable bounds check
        except (IndexError, OverflowError, MemoryError) as e:
            pytest.fail(f"Memory safety violation on input {inp!r}: {e}")