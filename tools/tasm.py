#!/usr/bin/env python3
import sys
import struct
import re
import zlib
import os

OPCODES = {
    "TNOP": 0,
    "TLDI": 1,
    "TMOV": 2,
    "TLD": 3,
    "TST": 4,
    "TADD": 5,
    "TSUB": 6,
    "TCMP": 7,
    "TSEL": 8,
    "TJMP": 9,
    "TBRNEG": 10,
    "TBRZERO": 11,
    "TBRPOS": 12,
    "TSEND": 13,
    "TRECV": 14,
    "TFLUSH": 15,
    "TINV": 16,
    "TSYS": 17,
    "THALT": 18,
    "TROUTE": 19,
    "TBIAS": 20,
    "TSENSE": 21,
}

SYSCALL_NAMES = {
    "LOG": 0,
    "UPTIME": 1,
    "CONFIG_GET": 2,
    "DELAY": 3,
}


def parse_ternary(lit):
    """Convert t+0- style literal to int."""
    val = 0
    power = 1
    digits = lit[1:][::-1]  # Remove 't' and reverse
    for d in digits:
        if d == "+":
            val += power
        elif d == "-":
            val -= power
        power *= 3
    return val


def parse_imm(lit, labels):
    if lit.startswith("t"):
        return parse_ternary(lit)
    if lit.startswith("@"):
        target = lit[1:]
        if target not in labels:
            raise ValueError(f"Unknown label: {target}")
        return labels[target]
    if lit.upper() in SYSCALL_NAMES:
        return SYSCALL_NAMES[lit.upper()]
    return int(lit)


def assemble(filename, output_name):
    with open(filename, "r") as f:
        lines = f.readlines()

    # Pass 1: Labels and directives
    labels = {}
    instr_lines = []
    entry_label = None
    for line in lines:
        line = line.split(";")[0].split("#")[0].strip()
        if not line:
            continue

        if line.startswith(".entry"):
            entry_label = line.split()[1]
        elif line.endswith(":"):
            labels[line[:-1]] = len(instr_lines)
        else:
            instr_lines.append(line)

    entry_ip = 0
    if entry_label is not None:
        if entry_label not in labels:
            raise ValueError(f"Unknown entry label: {entry_label}")
        entry_ip = labels[entry_label]

    # Pass 2: Encoding
    packed_instrs = []
    for idx, line in enumerate(instr_lines):
        # Normalize: commas to spaces, then split
        parts = re.split(r"[,\s]+", line)
        op_name = parts[0].upper()
        if op_name not in OPCODES:
            raise ValueError(f"Line {idx}: Unknown opcode {op_name}")

        op_id = OPCODES[op_name]
        operands = parts[1:]

        def parse_reg(token):
            if not token.startswith("r"):
                raise ValueError(f"Line {idx}: Expected register, got {token}")
            reg = int(token[1:])
            if reg < 0 or reg > 7:
                raise ValueError(f"Line {idx}: Register out of range: {token}")
            return reg

        dst = 0
        src_a = 0
        src_b = 0
        imm = 0

        if op_name in {"TNOP", "THALT"}:
            if operands:
                raise ValueError(f"Line {idx}: {op_name} takes no operands")
        elif op_name == "TLDI":
            if len(operands) != 2:
                raise ValueError(f"Line {idx}: TLDI requires DST, IMM")
            dst = parse_reg(operands[0])
            imm = parse_imm(operands[1], labels)
        elif op_name in {"TMOV", "TLD", "TST"}:
            if len(operands) != 2:
                raise ValueError(f"Line {idx}: {op_name} requires two registers")
            dst = parse_reg(operands[0])
            src_a = parse_reg(operands[1])
        elif op_name in {"TADD", "TSUB", "TCMP"}:
            if len(operands) != 3:
                raise ValueError(f"Line {idx}: {op_name} requires DST, SRC_A, SRC_B")
            dst = parse_reg(operands[0])
            src_a = parse_reg(operands[1])
            src_b = parse_reg(operands[2])
        elif op_name == "TSEL":
            if len(operands) != 5:
                raise ValueError(f"Line {idx}: TSEL requires DST, SEL, ZERO, NEG, POS")
            dst = parse_reg(operands[0])
            src_a = parse_reg(operands[1])
            src_b = parse_reg(operands[2])
            neg_reg = parse_reg(operands[3])
            pos_reg = parse_reg(operands[4])
            imm = (neg_reg & 0x07) | ((pos_reg & 0x07) << 3)
        elif op_name == "TJMP":
            if len(operands) != 1:
                raise ValueError(f"Line {idx}: TJMP requires TARGET")
            imm = parse_imm(operands[0], labels)
        elif op_name in {"TBRNEG", "TBRZERO", "TBRPOS"}:
            if len(operands) != 2:
                raise ValueError(f"Line {idx}: {op_name} requires SRC, TARGET")
            src_a = parse_reg(operands[0])
            imm = parse_imm(operands[1], labels)
        elif op_name == "TSEND":
            if len(operands) != 3:
                raise ValueError(f"Line {idx}: TSEND requires DST, SRC, OP")
            dst = parse_reg(operands[0])
            src_a = parse_reg(operands[1])
            imm = parse_imm(operands[2], labels)
        elif op_name == "TRECV":
            if len(operands) != 1:
                raise ValueError(f"Line {idx}: TRECV requires DST")
            dst = parse_reg(operands[0])
        elif op_name in {"TFLUSH", "TINV"}:
            if len(operands) != 1:
                raise ValueError(f"Line {idx}: {op_name} requires SRC")
            src_a = parse_reg(operands[0])
        elif op_name == "TSYS":
            if len(operands) != 4:
                raise ValueError(f"Line {idx}: TSYS requires DST, SRC_A, SRC_B, ID")
            dst = parse_reg(operands[0])
            src_a = parse_reg(operands[1])
            src_b = parse_reg(operands[2])
            imm = parse_imm(operands[3], labels)
        elif op_name == "TROUTE":
            if len(operands) != 3:
                raise ValueError(f"Line {idx}: TROUTE requires DST, SRC_A, IMM")
            dst = parse_reg(operands[0])
            src_a = parse_reg(operands[1])
            imm = parse_imm(operands[2], labels)
        elif op_name == "TBIAS":
            if len(operands) != 2:
                raise ValueError(f"Line {idx}: TBIAS requires DST, SRC_A")
            dst = parse_reg(operands[0])
            src_a = parse_reg(operands[1])
        elif op_name == "TSENSE":
            if len(operands) != 3:
                raise ValueError(f"Line {idx}: TSENSE requires DST, SRC_A, IMM")
            dst = parse_reg(operands[0])
            src_a = parse_reg(operands[1])
            imm = parse_imm(operands[2], labels)
        else:
            raise ValueError(f"Line {idx}: Unhandled opcode {op_name}")

        # Pack into 32-bit word (v2 format)
        word = op_id & 0x3F
        word |= (dst & 0x07) << 6
        word |= (src_a & 0x07) << 9
        word |= (src_b & 0x07) << 12

        # 17-bit signed immediate
        imm_bits = imm & 0x1FFFF
        word |= imm_bits << 15

        packed_instrs.append(word)

    # Payload for checksum
    payload = b""
    for instr in packed_instrs:
        payload += struct.pack("<I", instr)

    checksum = zlib.crc32(payload) & 0xFFFFFFFF

    # Build full binary image
    header = struct.pack(
        "<IHIHHH", 0x52465856, 2, checksum, entry_ip, len(packed_instrs), 0
    )
    image = header + payload

    if output_name.endswith(".c"):
        # C array output mode
        base = os.path.splitext(os.path.basename(filename))[0]
        base = re.sub(r"[^a-zA-Z0-9_]", "_", base)
        hex_bytes = ", ".join(f"0x{b:02X}" for b in image)
        c_src = (
            '#include <stdint.h>\n'
            '#include <stddef.h>\n\n'
            f'const uint8_t vm_program_{base}[] = {{ {hex_bytes} }};\n'
            f'const size_t vm_program_{base}_len = sizeof(vm_program_{base});\n'
        )
        with open(output_name, "w") as f:
            f.write(c_src)
        print(
            f"Assembled {len(packed_instrs)} instructions to {output_name} (C array, checksum: {checksum:08X})"
        )
    else:
        # Binary output
        with open(output_name, "wb") as f:
            f.write(image)
        print(
            f"Assembled {len(packed_instrs)} instructions to {output_name} (checksum: {checksum:08X})"
        )


def upload(filename, port, baud=115200):
    """Compile a .tasm file and upload to device via serial."""
    import tempfile
    tmp = tempfile.mktemp(suffix=".rfxv")
    assemble(filename, tmp)
    with open(tmp, "rb") as f:
        data = f.read()
    os.unlink(tmp)
    hex_str = data.hex()

    try:
        import serial
    except ImportError:
        print("Error: pyserial required. Install with: pip install pyserial")
        sys.exit(1)

    ser = serial.Serial(port, baud, timeout=3)
    import time
    time.sleep(0.5)
    ser.read(ser.in_waiting)
    cmd = f"vm loadhex {hex_str}\n"
    ser.write(cmd.encode())
    time.sleep(1.5)
    out = ser.read(ser.in_waiting).decode(errors="replace")
    ser.close()

    if "vm loaded" in out:
        print(f"Uploaded {len(data)} bytes to {port}")
    else:
        print(f"Upload may have failed. Device response:")
        for line in out.strip().split("\n"):
            if line.strip():
                print(f"  {line.strip()}")


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[2] == "--upload":
        upload(sys.argv[1], sys.argv[3])
    elif len(sys.argv) >= 3:
        assemble(sys.argv[1], sys.argv[2])
    else:
        print("Usage: tasm.py <input.tasm> <output.rfxv|output.c>")
        print("       tasm.py <input.tasm> --upload <port>")
        sys.exit(1)
