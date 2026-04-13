#!/usr/bin/env python3
import sys
import struct
import re

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
    return int(lit)


def assemble(filename, output_name):
    with open(filename, "r") as f:
        lines = f.readlines()

    # Pass 1: Labels
    labels = {}
    instr_lines = []
    for line in lines:
        line = line.split(";")[0].split("#")[0].strip()
        if not line:
            continue

        if line.endswith(":"):
            labels[line[:-1]] = len(instr_lines)
        else:
            instr_lines.append(line)

    # Pass 2: Encoding
    packed_instrs = []
    for idx, line in enumerate(instr_lines):
        # Normalize: commas to spaces, then split
        parts = re.split(r"[,\s]+", line)
        op_name = parts[0].upper()
        if op_name not in OPCODES:
            raise ValueError(f"Line {idx}: Unknown opcode {op_name}")

        op_id = OPCODES[op_name]
        dst = 0
        src_a = 0
        src_b = 0
        imm = 0

        # Simple greedy parser based on operand count
        operands = parts[1:]

        for i, token in enumerate(operands):
            if token.startswith("r"):
                reg = int(token[1:])
                if i == 0:
                    dst = reg
                elif i == 1:
                    src_a = reg
                elif i == 2:
                    src_b = reg
            else:
                imm = parse_imm(token, labels)

        # Handle TSEL multi-register packing into IMM
        if op_name == "TSEL":
            # Syntax: TSEL DST, SEL_REG, ZERO_REG, NEG_REG, POS_REG
            # Note: Current greedy parser needs help here.
            # We'll assume the 4th/5th tokens are the branches.
            if len(operands) >= 5:
                neg_reg = int(operands[3][1:])
                pos_reg = int(operands[4][1:])
                imm = (neg_reg & 0x07) | ((pos_reg & 0x07) << 3)

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

    checksum = sum(payload) & 0xFFFFFFFF

    # Write Header and Payload
    # Magic(4), Ver(2), Checksum(4), Entry(2), InstrCount(2), DataCount(2) = 16B
    with open(output_name, "wb") as f:
        f.write(
            struct.pack("<IHIHHH", 0x52465856, 2, checksum, 0, len(packed_instrs), 0)
        )
        f.write(payload)

    print(
        f"Assembled {len(packed_instrs)} instructions to {output_name} (checksum: {checksum:08X})"
    )


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: tasm.py <input.tasm> <output.rfxv>")
        sys.exit(1)
    assemble(sys.argv[1], sys.argv[2])
