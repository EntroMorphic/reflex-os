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


# How long to wait for the shell to answer a `vm loadhex`.
UPLOAD_REPLY_TIMEOUT_S = 5.0


def assemble(filename, output_name):
    with open(filename, "r") as f:
        lines = f.readlines()

    # Pass 1: Labels and directives
    #
    # instr_lines carries the source line number alongside the text. Pass 2 used
    # to report `enumerate(instr_lines)`, which counts *instructions*, so every
    # label, comment and blank line above a fault shifted the reported position:
    # a bad opcode on source line 5 was reported as "Line 1". An assembler whose
    # error messages point at the wrong line is worse than one that says nothing,
    # because the reader trusts it.
    labels = {}
    instr_lines = []
    entry_label = None
    for lineno, raw in enumerate(lines, start=1):
        line = raw.split(";")[0].split("#")[0].strip()
        if not line:
            continue

        if line.startswith(".entry"):
            parts = line.split()
            if len(parts) < 2:
                raise ValueError(f"Line {lineno}: .entry requires a label")
            if len(parts) > 2:
                raise ValueError(
                    f"Line {lineno}: .entry takes one label, got {len(parts) - 1}")
            if entry_label is not None:
                raise ValueError(f"Line {lineno}: .entry already declared")
            entry_label = parts[1]
        elif line.endswith(":"):
            name = line[:-1].strip()
            if not name:
                raise ValueError(f"Line {lineno}: empty label")
            # Silently overwriting meant `start:` twice assembled cleanly and
            # resolved every reference to the second definition -- the branch
            # target moves, the program still runs, and nothing says so.
            if name in labels:
                raise ValueError(f"Line {lineno}: duplicate label '{name}'")
            labels[name] = len(instr_lines)
        else:
            instr_lines.append((lineno, line))

    entry_ip = 0
    if entry_label is not None:
        if entry_label not in labels:
            raise ValueError(f"Unknown entry label: {entry_label}")
        entry_ip = labels[entry_label]

    # Pass 2: Encoding
    packed_instrs = []
    for idx, (lineno, line) in enumerate(instr_lines):
        # Normalize: commas to spaces, then split
        parts = re.split(r"[,\s]+", line)
        op_name = parts[0].upper()
        if op_name not in OPCODES:
            raise ValueError(f"Line {lineno}: Unknown opcode {op_name}")

        op_id = OPCODES[op_name]
        operands = parts[1:]

        def parse_reg(token):
            if not token.startswith("r"):
                raise ValueError(f"Line {lineno}: Expected register, got {token}")
            reg = int(token[1:])
            if reg < 0 or reg > 7:
                raise ValueError(f"Line {lineno}: Register out of range: {token}")
            return reg

        dst = 0
        src_a = 0
        src_b = 0
        imm = 0

        if op_name in {"TNOP", "THALT"}:
            if operands:
                raise ValueError(f"Line {lineno}: {op_name} takes no operands")
        elif op_name == "TLDI":
            if len(operands) != 2:
                raise ValueError(f"Line {lineno}: TLDI requires DST, IMM")
            dst = parse_reg(operands[0])
            imm = parse_imm(operands[1], labels)
        elif op_name in {"TMOV", "TLD", "TST"}:
            if len(operands) != 2:
                raise ValueError(f"Line {lineno}: {op_name} requires two registers")
            dst = parse_reg(operands[0])
            src_a = parse_reg(operands[1])
        elif op_name in {"TADD", "TSUB", "TCMP"}:
            if len(operands) != 3:
                raise ValueError(f"Line {lineno}: {op_name} requires DST, SRC_A, SRC_B")
            dst = parse_reg(operands[0])
            src_a = parse_reg(operands[1])
            src_b = parse_reg(operands[2])
        elif op_name == "TSEL":
            if len(operands) != 5:
                raise ValueError(f"Line {lineno}: TSEL requires DST, SEL, ZERO, NEG, POS")
            dst = parse_reg(operands[0])
            src_a = parse_reg(operands[1])
            src_b = parse_reg(operands[2])
            neg_reg = parse_reg(operands[3])
            pos_reg = parse_reg(operands[4])
            imm = (neg_reg & 0x07) | ((pos_reg & 0x07) << 3)
        elif op_name == "TJMP":
            if len(operands) != 1:
                raise ValueError(f"Line {lineno}: TJMP requires TARGET")
            imm = parse_imm(operands[0], labels)
        elif op_name in {"TBRNEG", "TBRZERO", "TBRPOS"}:
            if len(operands) != 2:
                raise ValueError(f"Line {lineno}: {op_name} requires SRC, TARGET")
            src_a = parse_reg(operands[0])
            imm = parse_imm(operands[1], labels)
        elif op_name == "TSEND":
            if len(operands) != 3:
                raise ValueError(f"Line {lineno}: TSEND requires DST, SRC, OP")
            dst = parse_reg(operands[0])
            src_a = parse_reg(operands[1])
            imm = parse_imm(operands[2], labels)
        elif op_name == "TRECV":
            if len(operands) != 1:
                raise ValueError(f"Line {lineno}: TRECV requires DST")
            dst = parse_reg(operands[0])
        elif op_name in {"TFLUSH", "TINV"}:
            if len(operands) != 1:
                raise ValueError(f"Line {lineno}: {op_name} requires SRC")
            src_a = parse_reg(operands[0])
        elif op_name == "TSYS":
            if len(operands) != 4:
                raise ValueError(f"Line {lineno}: TSYS requires DST, SRC_A, SRC_B, ID")
            dst = parse_reg(operands[0])
            src_a = parse_reg(operands[1])
            src_b = parse_reg(operands[2])
            imm = parse_imm(operands[3], labels)
        elif op_name == "TROUTE":
            if len(operands) != 3:
                raise ValueError(f"Line {lineno}: TROUTE requires DST, SRC_A, IMM")
            dst = parse_reg(operands[0])
            src_a = parse_reg(operands[1])
            imm = parse_imm(operands[2], labels)
        elif op_name == "TBIAS":
            if len(operands) != 2:
                raise ValueError(f"Line {lineno}: TBIAS requires DST, SRC_A")
            dst = parse_reg(operands[0])
            src_a = parse_reg(operands[1])
        elif op_name == "TSENSE":
            if len(operands) != 3:
                raise ValueError(f"Line {lineno}: TSENSE requires DST, SRC_A, IMM")
            dst = parse_reg(operands[0])
            src_a = parse_reg(operands[1])
            imm = parse_imm(operands[2], labels)
        else:
            raise ValueError(f"Line {lineno}: Unhandled opcode {op_name}")

        # Pack into 32-bit word (v2 format)
        word = op_id & 0x3F
        word |= (dst & 0x07) << 6
        word |= (src_a & 0x07) << 9
        word |= (src_b & 0x07) << 12

        # 17-bit signed immediate.
        #
        # Range-check before masking. This used to mask straight to 0x1FFFF, so
        # a constant the format cannot hold was silently reinterpreted rather
        # than rejected — `TLDI r0, 100000` assembled cleanly and loaded as
        # 34464. The loader cannot catch it either: after truncation the value
        # is a perfectly legal 17-bit immediate. Registers were already checked
        # this way; the immediate was not.
        IMM_MIN, IMM_MAX = -(1 << 16), (1 << 16) - 1
        if imm < IMM_MIN or imm > IMM_MAX:
            raise ValueError(
                f"Line {lineno}: immediate {imm} out of range "
                f"for the 17-bit signed field ({IMM_MIN}..{IMM_MAX})")
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
    import time

    # mkstemp, and removed in a finally: the old mktemp+unlink pair left the
    # temporary image behind whenever assemble() raised, which is exactly when
    # something went wrong.
    fd, tmp = tempfile.mkstemp(suffix=".rfxv")
    os.close(fd)
    try:
        assemble(filename, tmp)
        with open(tmp, "rb") as f:
            data = f.read()
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)

    hex_str = data.hex()
    if len(hex_str) + len("vm loadhex \n") > 1024:
        print(f"Error: program too large for shell upload ({len(data)} bytes, max ~500)")
        sys.exit(1)

    try:
        import serial
    except ImportError:
        print("Error: pyserial required. Install with: pip install pyserial")
        sys.exit(1)

    try:
        ser = serial.Serial(port, baud, timeout=3)
    except serial.SerialException as e:
        print(f"Error: cannot open {port}: {e}")
        sys.exit(1)

    # try/finally around everything that touches the port. Any exception
    # between open and close previously leaked the fd, which on macOS leaves
    # the port claimed until the interpreter exits.
    try:
        time.sleep(0.5)
        ser.read(ser.in_waiting)
        ser.write(f"vm loadhex {hex_str}\n".encode())

        # Read until the device answers or the deadline passes, rather than
        # sleeping a fixed 1.5s and taking whatever happened to have arrived.
        # A board slower than that reported a false failure on a timing miss
        # alone -- and the upload had in fact succeeded, which is the worst
        # shape a diagnostic can take.
        deadline = time.monotonic() + UPLOAD_REPLY_TIMEOUT_S
        out = ""
        while time.monotonic() < deadline:
            chunk = ser.read(ser.in_waiting or 1)
            if chunk:
                out += chunk.decode(errors="replace")
                if "vm loaded" in out or "#R:" in out:
                    break
            else:
                time.sleep(0.05)
    finally:
        ser.close()

    if "vm loaded" in out:
        print(f"Uploaded {len(data)} bytes to {port}")
        return

    print("Upload may have failed. Device response:")
    if not out.strip():
        print(f"  (no response within {UPLOAD_REPLY_TIMEOUT_S:g}s)")
    for line in out.strip().split("\n"):
        if line.strip():
            print(f"  {line.strip()}")
    sys.exit(1)


def _usage(msg=None):
    if msg:
        print(f"Error: {msg}")
    print("Usage: tasm.py <input.tasm> <output.rfxv|output.c>")
    print("       tasm.py <input.tasm> --upload <port>")
    sys.exit(1)


if __name__ == "__main__":
    args = sys.argv[1:]
    if len(args) < 2:
        _usage()
    elif args[1] == "--upload":
        # `tasm.py in.tasm --upload` with the port omitted used to fall through
        # to the assemble branch and write an output file literally named
        # "--upload", reporting success for an upload that never happened.
        if len(args) < 3:
            _usage("--upload requires a port, e.g. --upload /dev/cu.usbmodem1101")
        upload(args[0], args[2])
    elif args[1].startswith("--"):
        _usage(f"unknown option {args[1]}")
    else:
        try:
            assemble(args[0], args[1])
        except (ValueError, OSError) as e:
            print(f"Error: {e}")
            sys.exit(1)
