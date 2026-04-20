#!/usr/bin/env python3
"""Tests for the TASM assembler (tools/tasm.py)."""
import subprocess
import struct
import tempfile
import os
import sys
import zlib

# Add tools dir to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'tools'))
import tasm


def test_encode_nop():
    """TNOP should produce opcode=0, all fields zero → word 0x00000000."""
    src = "; nop test\n.entry start\nstart:\n    TNOP\n"
    with tempfile.NamedTemporaryFile(mode='w', suffix='.tasm', delete=False) as f:
        f.write(src)
        f.flush()
        outfile = f.name + '.rfxv'
    try:
        tasm.assemble(f.name, outfile)
        with open(outfile, 'rb') as out:
            data = out.read()
        # Header is 16 bytes
        assert len(data) == 16 + 4, f"Expected 20 bytes, got {len(data)}"
        # Instruction word
        word = struct.unpack_from('<I', data, 16)[0]
        assert word == 0x00000000, f"NOP word should be 0, got 0x{word:08X}"
        # Verify entry_ip = 0
        entry_ip = struct.unpack_from('<H', data, 10)[0]
        assert entry_ip == 0
    finally:
        os.unlink(f.name)
        if os.path.exists(outfile):
            os.unlink(outfile)
    print("PASS: test_encode_nop")


def test_tldi():
    """TLDI r3, 42 should encode opcode=1, dst=3, imm=42."""
    src = "TLDI r3, 42\n"
    with tempfile.NamedTemporaryFile(mode='w', suffix='.tasm', delete=False) as f:
        f.write(src)
        f.flush()
        outfile = f.name + '.rfxv'
    try:
        tasm.assemble(f.name, outfile)
        with open(outfile, 'rb') as out:
            data = out.read()
        word = struct.unpack_from('<I', data, 16)[0]
        opcode = word & 0x3F
        dst = (word >> 6) & 0x07
        imm = (word >> 15) & 0x1FFFF
        assert opcode == 1, f"Expected opcode 1, got {opcode}"
        assert dst == 3, f"Expected dst=3, got {dst}"
        assert imm == 42, f"Expected imm=42, got {imm}"
    finally:
        os.unlink(f.name)
        if os.path.exists(outfile):
            os.unlink(outfile)
    print("PASS: test_tldi")


def test_label_resolution():
    """Labels should resolve to instruction indices."""
    src = "TNOP\nloop:\nTJMP @loop\n"
    with tempfile.NamedTemporaryFile(mode='w', suffix='.tasm', delete=False) as f:
        f.write(src)
        f.flush()
        outfile = f.name + '.rfxv'
    try:
        tasm.assemble(f.name, outfile)
        with open(outfile, 'rb') as out:
            data = out.read()
        # TJMP is second instruction (index 1)
        word = struct.unpack_from('<I', data, 16 + 4)[0]
        opcode = word & 0x3F
        imm = (word >> 15) & 0x1FFFF
        assert opcode == 9, f"Expected TJMP opcode 9, got {opcode}"
        # Label 'loop' is at instruction index 1
        assert imm == 1, f"Expected imm=1, got {imm}"
    finally:
        os.unlink(f.name)
        if os.path.exists(outfile):
            os.unlink(outfile)
    print("PASS: test_label_resolution")


def test_entry_directive():
    """.entry sets entry_ip in the header."""
    src = ".entry start\nTNOP\nstart:\nTHALT\n"
    with tempfile.NamedTemporaryFile(mode='w', suffix='.tasm', delete=False) as f:
        f.write(src)
        f.flush()
        outfile = f.name + '.rfxv'
    try:
        tasm.assemble(f.name, outfile)
        with open(outfile, 'rb') as out:
            data = out.read()
        entry_ip = struct.unpack_from('<H', data, 10)[0]
        assert entry_ip == 1, f"Expected entry_ip=1, got {entry_ip}"
    finally:
        os.unlink(f.name)
        if os.path.exists(outfile):
            os.unlink(outfile)
    print("PASS: test_entry_directive")


def test_syscall_names():
    """Named syscall IDs should resolve to correct immediate values."""
    src = "TSYS r0, r1, r2, DELAY\n"
    with tempfile.NamedTemporaryFile(mode='w', suffix='.tasm', delete=False) as f:
        f.write(src)
        f.flush()
        outfile = f.name + '.rfxv'
    try:
        tasm.assemble(f.name, outfile)
        with open(outfile, 'rb') as out:
            data = out.read()
        word = struct.unpack_from('<I', data, 16)[0]
        imm = (word >> 15) & 0x1FFFF
        assert imm == 3, f"Expected DELAY=3, got {imm}"
    finally:
        os.unlink(f.name)
        if os.path.exists(outfile):
            os.unlink(outfile)
    print("PASS: test_syscall_names")


def test_c_output():
    """C array output mode should produce valid C source."""
    src = "TNOP\nTHALT\n"
    with tempfile.NamedTemporaryFile(mode='w', suffix='.tasm', delete=False) as f:
        f.write(src)
        f.flush()
        outfile = f.name.replace('.tasm', '') + '_out.c'
    try:
        tasm.assemble(f.name, outfile)
        with open(outfile, 'r') as out:
            c_src = out.read()
        assert '#include <stdint.h>' in c_src
        assert 'const uint8_t vm_program_' in c_src
        assert 'const size_t vm_program_' in c_src
        assert '0x56, 0x58' in c_src  # Magic bytes (LE): 0x52465856 → 56 58 46 52
    finally:
        os.unlink(f.name)
        if os.path.exists(outfile):
            os.unlink(outfile)
    print("PASS: test_c_output")


def test_goose_opcodes():
    """TROUTE, TBIAS, TSENSE should assemble correctly."""
    src = "TROUTE r0, r1, 1\nTBIAS r2, r3\nTSENSE r4, r5, 0\n"
    with tempfile.NamedTemporaryFile(mode='w', suffix='.tasm', delete=False) as f:
        f.write(src)
        f.flush()
        outfile = f.name + '.rfxv'
    try:
        tasm.assemble(f.name, outfile)
        with open(outfile, 'rb') as out:
            data = out.read()
        # TROUTE: opcode=19
        w0 = struct.unpack_from('<I', data, 16)[0]
        assert (w0 & 0x3F) == 19, f"Expected TROUTE=19, got {w0 & 0x3F}"
        # TBIAS: opcode=20
        w1 = struct.unpack_from('<I', data, 20)[0]
        assert (w1 & 0x3F) == 20, f"Expected TBIAS=20, got {w1 & 0x3F}"
        # TSENSE: opcode=21
        w2 = struct.unpack_from('<I', data, 24)[0]
        assert (w2 & 0x3F) == 21, f"Expected TSENSE=21, got {w2 & 0x3F}"
    finally:
        os.unlink(f.name)
        if os.path.exists(outfile):
            os.unlink(outfile)
    print("PASS: test_goose_opcodes")


def test_checksum():
    """CRC32 in header should match payload."""
    src = "TLDI r0, 7\nTHALT\n"
    with tempfile.NamedTemporaryFile(mode='w', suffix='.tasm', delete=False) as f:
        f.write(src)
        f.flush()
        outfile = f.name + '.rfxv'
    try:
        tasm.assemble(f.name, outfile)
        with open(outfile, 'rb') as out:
            data = out.read()
        stored_crc = struct.unpack_from('<I', data, 6)[0]
        payload = data[16:]
        computed_crc = zlib.crc32(payload) & 0xFFFFFFFF
        assert stored_crc == computed_crc, f"CRC mismatch: stored={stored_crc:08X} computed={computed_crc:08X}"
    finally:
        os.unlink(f.name)
        if os.path.exists(outfile):
            os.unlink(outfile)
    print("PASS: test_checksum")


def test_examples_compile():
    """Verify all example .tasm files compile without error."""
    examples_dir = os.path.join(os.path.dirname(__file__), '..', '..', 'examples', 'tasm')
    examples_dir = os.path.abspath(examples_dir)
    if not os.path.isdir(examples_dir):
        print("SKIP: test_examples_compile (no examples dir)")
        return
    count = 0
    for fname in os.listdir(examples_dir):
        if not fname.endswith('.tasm'):
            continue
        src_path = os.path.join(examples_dir, fname)
        with tempfile.NamedTemporaryFile(suffix='.rfxv', delete=False) as tmp:
            outfile = tmp.name
        try:
            tasm.assemble(src_path, outfile)
            count += 1
        finally:
            if os.path.exists(outfile):
                os.unlink(outfile)
    assert count >= 3, f"Expected at least 3 examples, compiled {count}"
    print(f"PASS: test_examples_compile ({count} programs)")


if __name__ == "__main__":
    test_encode_nop()
    test_tldi()
    test_label_resolution()
    test_entry_directive()
    test_syscall_names()
    test_c_output()
    test_goose_opcodes()
    test_checksum()
    test_examples_compile()
    print("\nAll tests passed.")
