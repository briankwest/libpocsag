# libpocsag

C library for **POCSAG** (Post Office Code Standardisation Advisory Group) paging protocol encoding and decoding. Generates and parses complete POCSAG bitstreams including preamble, sync words, BCH error correction, and multi-batch message spanning.

C99, no external dependencies beyond libc. All structs are stack-allocatable with zero dynamic memory allocation.

## Table of Contents

- [Features](#features)
- [Building](#building)
- [API](#api)
  - [Single Message Encoding](#single-message-encoding)
  - [Batch Encoding](#batch-encoding)
  - [Streaming Decoder](#streaming-decoder)
  - [BCH Error Correction](#bch-error-correction)
  - [Error Codes](#error-codes)
- [Protocol Details](#protocol-details)
- [Test Suite](#test-suite)
- [Example Programs](#example-programs)
- [Project Structure](#project-structure)
- [Technical Details](#technical-details)
- [License](#license)

## Features

- **POCSAG Encoder** -- Single or batch message encoding to complete POCSAG bitstreams with preamble, sync words, frame assignment, and idle fill
- **POCSAG Decoder** -- Streaming state-machine decoder with sync detection, callback-driven message delivery, and bit-level or byte-level input
- **BCH(31,21) Codec** -- Full encode, syndrome check, single-bit error correction, and double-bit error detection
- **Numeric Messages** -- BCD encoding/decoding with digits 0-9 and special characters `*U -[]`
- **Alphanumeric Messages** -- 7-bit ASCII packing/unpacking with LSB-first bit ordering per the CCIR specification
- **Tone-Only Pages** -- Address-only pages with no message data
- **Multi-Batch Spanning** -- Long messages automatically span multiple batches with correct continuation
- **Multi-Message Batching** -- Multiple messages packed efficiently into shared batches by frame assignment
- **Zero Allocation** -- All encoder and decoder contexts are fixed-size structs, stack-allocatable with no malloc

### Supported Parameters

| Parameter | Values |
|-----------|--------|
| Addresses | 0 -- 2,097,151 (21-bit) |
| Function codes | 0 (numeric), 1-2 (tone), 3 (alphanumeric) |
| Message types | Numeric, alphanumeric, tone-only |
| Numeric characters | `0123456789*U -[]` |
| Alpha characters | 7-bit ASCII (0x01 -- 0x7F) |
| Baud rates | 512, 1200, 2400 (bitstream is rate-agnostic) |
| Messages per batch | Up to 16 queued |
| BCH correction | Single-bit correction, double-bit detection |

## Building

```bash
./autogen.sh    # generate configure (requires autoconf, automake, libtool)
./configure
make            # builds libpocsag.so, libpocsag.a, and example programs
make check      # runs the test suite (46 tests)
make install    # installs library, headers, and pkg-config file
```

Compiles with `-Wall -Wextra -pedantic` in C99 strict mode. Requires only a C99 compiler and libc. No libm or other dependencies.

### Debian Packages

```bash
dpkg-buildpackage -us -uc -b
```

Produces `libpocsag0` (shared library), `libpocsag-dev` (headers + pkg-config), and `libpocsag0-dbgsym` (debug symbols).

## API

All public functions return `pocsag_err_t` (0 on success). Headers are under `include/libpocsag/`, included via `<libpocsag/pocsag.h>`.

### Single Message Encoding

```c
#include <libpocsag/pocsag.h>

uint8_t buf[POCSAG_BITSTREAM_MAX];
size_t len, bits;

/* Encode a numeric page */
pocsag_encode_single(1234000, POCSAG_FUNC_NUMERIC, POCSAG_MSG_NUMERIC,
                     "5551234", buf, sizeof(buf), &len, &bits);
/* buf now contains the complete POCSAG bitstream (preamble + batches) */
/* len = bytes written, bits = exact bit count */

/* Encode an alphanumeric page */
pocsag_encode_single(2000, POCSAG_FUNC_ALPHA, POCSAG_MSG_ALPHA,
                     "Hello, World!", buf, sizeof(buf), &len, &bits);

/* Encode a tone-only page */
pocsag_encode_single(500, POCSAG_FUNC_TONE1, POCSAG_MSG_TONE_ONLY,
                     NULL, buf, sizeof(buf), &len, &bits);
```

### Batch Encoding

Queue multiple messages for efficient packing into shared batches:

```c
pocsag_encoder_t enc;
pocsag_encoder_init(&enc);

/* Queue messages for different pagers */
pocsag_encoder_add(&enc, 1000, POCSAG_FUNC_NUMERIC,
                   POCSAG_MSG_NUMERIC, "5551234");
pocsag_encoder_add(&enc, 2000, POCSAG_FUNC_ALPHA,
                   POCSAG_MSG_ALPHA, "Meeting at 3pm");
pocsag_encoder_add(&enc, 3000, POCSAG_FUNC_TONE1,
                   POCSAG_MSG_TONE_ONLY, NULL);

/* Encode all messages into a single bitstream */
uint8_t buf[POCSAG_BITSTREAM_MAX];
size_t len, bits;
pocsag_encode(&enc, buf, sizeof(buf), &len, &bits);

/* Messages are sorted by frame and packed into batches.
 * Different addresses in different frames share the same batch. */
```

### Streaming Decoder

Feed bits or bytes incrementally. The callback fires for each complete message:

```c
void on_message(const pocsag_msg_t *msg, void *user)
{
    printf("addr=%u func=%d type=%d msg=\"%s\"\n",
           msg->address, msg->function, msg->type, msg->text);
}

pocsag_decoder_t dec;
pocsag_decoder_init(&dec, on_message, NULL);

/* Feed packed bytes (MSB-first, e.g., from a file or demodulator) */
pocsag_decoder_feed_bytes(&dec, data, nbytes);

/* Or feed individual bits (one bit per byte, 0 or 1) */
pocsag_decoder_feed_bits(&dec, bit_array, nbit);

/* Flush any partially assembled message at end of stream */
pocsag_decoder_flush(&dec);

/* Check statistics */
printf("codewords=%u corrected=%u errors=%u messages=%u\n",
       dec.stat_codewords, dec.stat_corrected,
       dec.stat_errors, dec.stat_messages);
```

The decoder handles:
- Sync word detection from arbitrary bit positions
- BCH single-bit error correction on every codeword
- Messages spanning multiple batches
- Automatic numeric/alpha decoding based on function code

### BCH Error Correction

The BCH(31,21) codec is exposed for direct codeword manipulation:

```c
#include <libpocsag/bch.h>

/* Build a valid 32-bit codeword from 21 data bits */
uint32_t cw = pocsag_codeword_build(data21);

/* Check a codeword (returns 0 if valid) */
uint32_t syndrome = pocsag_bch_syndrome(cw);

/* Attempt single-bit error correction (returns 0 on success) */
int rc = pocsag_bch_correct(&cw);

/* Verify even parity (returns 1 if valid) */
int ok = pocsag_parity_check(cw);
```

### Error Codes

| Code | Constant | Meaning |
|------|----------|---------|
| 0 | `POCSAG_OK` | Success |
| -1 | `POCSAG_ERR_PARAM` | Invalid argument or NULL pointer |
| -2 | `POCSAG_ERR_OVERFLOW` | Output buffer too small |
| -3 | `POCSAG_ERR_BADCHAR` | Message contains invalid character for encoding |
| -4 | `POCSAG_ERR_BCH` | Uncorrectable BCH error |
| -5 | `POCSAG_ERR_SYNC` | Sync word not found or lost |
| -6 | `POCSAG_ERR_STATE` | Invalid state for operation |

## Protocol Details

POCSAG (CCIR Radio Paging Code No. 1) is a one-way digital paging protocol widely used for pager systems. Key parameters:

| Parameter | Value |
|-----------|-------|
| Baud rates | 512, 1200, 2400 bps |
| Modulation | FSK (this library handles the digital bitstream) |
| Sync codeword | `0x7CD215D8` |
| Idle codeword | `0x7A89C197` |
| Preamble | 576 bits alternating 1/0 |
| Batch size | 1 sync + 16 codewords (8 frames x 2 slots) |
| Codeword | 32 bits: 1 type + 20 data + 10 BCH + 1 parity |
| Address range | 0 -- 2,097,151 (21-bit) |
| Frame assignment | address % 8 |
| Error correction | BCH(31,21) with generator polynomial x^10+x^9+x^8+x^6+x^5+x^3+1 |

### Bitstream Structure

```
[  576-bit preamble (0xAA...)  ]
[  32-bit sync codeword        ]
[  32-bit codeword slot  0     ]  <- frame 0, slot 0
[  32-bit codeword slot  1     ]  <- frame 0, slot 1
[  32-bit codeword slot  2     ]  <- frame 1, slot 0
[  ...                         ]
[  32-bit codeword slot 15     ]  <- frame 7, slot 1
[  32-bit sync codeword        ]  <- next batch (if needed)
[  ...                         ]
```

### Codeword Format

```
Address codeword:
  bit 31    = 0 (address flag)
  bits 30-13 = address >> 3 (18 bits)
  bits 12-11 = function code (2 bits)
  bits 10-1  = BCH(31,21) parity (10 bits)
  bit 0     = even parity

Message codeword:
  bit 31    = 1 (message flag)
  bits 30-11 = message data (20 bits)
  bits 10-1  = BCH(31,21) parity (10 bits)
  bit 0     = even parity
```

## Test Suite

```
$ make check

libpocsag test suite

BCH:
  test_bch_sync_valid                                     PASS
  test_bch_idle_valid                                     PASS
  test_bch_build_verify                                   PASS
  test_bch_correct_single                                 PASS
  test_bch_correct_parity_bit                             PASS
  test_bch_detect_double                                  PASS
  test_bch_encode_zero                                    PASS
  test_bch_all_bits_correctable                           PASS

Codeword:
  test_cw_address_valid_bch                               PASS
  test_cw_address_type_bit                                PASS
  test_cw_address_fields                                  PASS
  test_cw_message_type_bit                                PASS
  test_cw_message_valid_bch                               PASS
  test_cw_message_data                                    PASS
  test_cw_frame_assignment                                PASS

Numeric:
  test_numeric_roundtrip_short                            PASS
  test_numeric_roundtrip_long                             PASS
  test_numeric_special_chars                              PASS
  test_numeric_padding                                    PASS
  test_numeric_bad_char                                   PASS
  test_numeric_empty                                      PASS

Alpha:
  test_alpha_roundtrip_short                              PASS
  test_alpha_roundtrip_long                               PASS
  test_alpha_special                                      PASS
  test_alpha_single_char                                  PASS
  test_alpha_chunk_count                                  PASS

Encoder:
  test_enc_single_numeric                                 PASS
  test_enc_single_alpha                                   PASS
  test_enc_tone_only                                      PASS
  test_enc_preamble                                       PASS
  test_enc_sync_present                                   PASS
  test_enc_all_codewords_valid                            PASS
  test_enc_multi_message                                  PASS
  test_enc_bad_address                                    PASS

Decoder:
  test_dec_basic_numeric                                  PASS
  test_dec_basic_alpha                                    PASS
  test_dec_tone_only                                      PASS
  test_dec_feed_bits                                      PASS
  test_dec_stats                                          PASS
  test_dec_reset                                          PASS

Roundtrip:
  test_rt_numeric                                         PASS
  test_rt_alpha                                           PASS
  test_rt_tone                                            PASS
  test_rt_multi_message                                   PASS
  test_rt_max_address                                     PASS
  test_rt_address_zero                                    PASS

46 passed, 0 failed
```

Tests cover:
- BCH(31,21) encode/syndrome/correct for all 32 bit positions, known sync and idle codewords
- Address and message codeword construction with field extraction verification
- Numeric BCD encoding/decoding with all special characters, padding, and invalid input
- 7-bit alpha encoding/decoding round-trips for short, long, and special-character messages
- Encoder output validation: preamble bytes, sync word position, BCH validity on every codeword
- Streaming decoder with both byte and bit feed interfaces, statistics tracking
- Full encode-then-decode round-trips for numeric, alpha, tone-only, multi-message, and edge-case addresses

## Example Programs

| Program | Description |
|---------|-------------|
| `encode_page` | Encode a pager message, write raw POCSAG bitstream to stdout |
| `decode_stream` | Read raw POCSAG bitstream from stdin, print decoded messages |

```bash
# Encode a numeric page
./encode_page 1234 n "5551234" > page.bin

# Encode an alphanumeric page
./encode_page 2000 a "Hello POCSAG" > page.bin

# Encode a tone-only page
./encode_page 500 t > page.bin

# Decode a bitstream
./decode_stream < page.bin
# [ALPHA] addr=2000 func=3 msg="Hello POCSAG"
# codewords=16 corrected=0 errors=0 messages=1

# Pipe encode directly to decode
./encode_page 1234 n "5551234" | ./decode_stream
# [NUM] addr=1234 func=0 msg="5551234"
```

## Project Structure

```
libpocsag/
  include/libpocsag/
    pocsag.h            Umbrella header (includes all public headers)
    version.h           Version constants
    error.h             Error codes and pocsag_strerror()
    types.h             Core types, protocol constants, message struct
    bch.h               BCH(31,21) encode, syndrome, correct, parity
    encoder.h           Batch encoder API
    decoder.h           Streaming decoder API with callback
  src/
    pocsag_internal.h   Private types, bitstream writer, internal prototypes
    error.c             Error string table
    bch.c               BCH(31,21) codec (encode, syndrome, 1-bit correct)
    codeword.c          Address and message codeword construction
    numeric.c           BCD numeric encode/decode
    alpha.c             7-bit ASCII pack/unpack (LSB-first per CCIR spec)
    encoder.c           Batch encoder with frame packing and continuation
    decoder.c           Streaming state-machine decoder
  tests/
    test.h              Test harness macros (ASSERT, RUN_TEST, etc.)
    test_main.c         Test runner
    test_bch.c          BCH encode/syndrome/correct tests (8 tests)
    test_codeword.c     Codeword construction tests (7 tests)
    test_numeric.c      BCD numeric round-trip tests (6 tests)
    test_alpha.c        Alpha encoding round-trip tests (5 tests)
    test_encoder.c      Encoder output validation tests (8 tests)
    test_decoder.c      Streaming decoder tests (6 tests)
    test_roundtrip.c    Full encode-decode round-trip tests (6 tests)
  examples/
    encode_page.c       Encode a page to raw bitstream on stdout
    decode_stream.c     Decode raw bitstream from stdin
  debian/               Debian packaging files
  configure.ac          Autoconf configuration
  Makefile.am           Top-level automake
  autogen.sh            Bootstrap script
  libpocsag.pc.in       pkg-config template
```

## Technical Details

### BCH(31,21) Error Correcting Code

The POCSAG BCH code uses generator polynomial g(x) = x^10 + x^9 + x^8 + x^6 + x^5 + x^3 + 1 (0x769). It protects the 31-bit field (bits 31-1 of the codeword) and can:
- Correct any single-bit error in the 31-bit BCH field
- Correct a single-bit error in the parity bit (bit 0) via separate even parity check
- Detect all double-bit errors

Error correction uses trial-flip over all 31 BCH-protected bit positions plus the parity bit. Syndrome computation is polynomial division by g(x).

### Encoder

The encoder generates a complete POCSAG bitstream:

1. **Preamble**: 576 bits of alternating 1/0 (72 bytes of 0xAA)
2. **Batches**: Each batch is a 32-bit sync codeword followed by 16 data codewords (8 frames x 2 slots)
3. **Frame Assignment**: Each message's address determines its frame (address % 8). The address codeword is placed at the frame's first available slot
4. **Multi-Message Packing**: Messages targeting different frames share the same batch. A frame limit prevents message data from overflowing into another message's reserved frame
5. **Batch Continuation**: Messages too long for one batch continue at slot 0 of the next batch, allowing the decoder to seamlessly append continuation data
6. **Idle Fill**: Unused slots are filled with the idle codeword (0x7A89C197)

### Decoder

The streaming decoder is a two-state machine:

1. **HUNTING**: Shifts bits into a 32-bit register, checking for the sync codeword. Transitions to BATCH when sync is found
2. **BATCH**: Accumulates 32-bit codewords. After each codeword: BCH error correction, type detection (idle/address/message), and message assembly. After 16 codewords, returns to HUNTING for the next sync

Message delivery triggers:
- An idle codeword after active message data
- A new address codeword (delivers the previous message, starts a new one)
- End of stream (via `pocsag_decoder_flush`)
- Messages spanning batches remain active across the HUNTING/sync transition

### Numeric Encoding (BCD)

Each character maps to a 4-bit BCD value, packed 5 digits per 20-bit message codeword data field:

| Character | BCD Value |
|-----------|-----------|
| `0`-`9` | 0x0-0x9 |
| `*` | 0xA |
| `U` | 0xB |
| ` ` (space) | 0xC |
| `-` | 0xD |
| `[` | 0xE |
| `]` | 0xF |

Partial codewords are padded with space (0xC). Trailing spaces are stripped on decode.

### Alphanumeric Encoding

7-bit ASCII characters are packed into 20-bit message codeword data fields. Per the CCIR Radio Paging Code No. 1 specification:
- Each character is transmitted least significant bit first
- Characters are packed contiguously across codeword boundaries
- An EOT character (0x04) terminates the message
- Decoding stops at NUL (0x00) or EOT (0x04)

A 20-bit data field holds 2 complete 7-bit characters plus 6 bits of a third character.

## License

MIT -- see [LICENSE](LICENSE).
