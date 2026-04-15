import struct
import sys

# LoomScript Compiler (loomc)
# Translates .ls (declarative manifolds) into .loom (binary fragments)


def compile_loom(input_file, output_file):
    print(f"LoomScript: Compiling {input_file}...")

    cells = []
    routes = []

    with open(input_file, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("//"):
                continue

            # Simple Parser: weave <src> -> <snk>
            if line.startswith("weave "):
                parts = line.replace("weave ", "").split("->")
                src_name = parts[0].strip()
                snk_name = parts[1].strip()

                # Manage name table
                if src_name not in cells:
                    cells.append(src_name)
                if snk_name not in cells:
                    cells.append(snk_name)

                routes.append(
                    {
                        "src_idx": cells.index(src_name),
                        "snk_idx": cells.index(snk_name),
                        "orientation": 1,
                        "coupling": 1,  # Software
                    }
                )

    # Header: Magic(4), Ver(4), NameHash(4), Cells(2), Routes(2), Trans(2)
    header = struct.pack(
        "<IIIHHH", 0x4D4F4F4C, 1, 0xABCDEF01, len(cells), len(routes), 0
    )

    with open(output_file, "wb") as f:
        f.write(header)
        # Write Cell Table
        for cname in cells:
            name_buf = cname.encode("ascii")[:23] + b"\0"
            f.write(name_buf.ljust(24, b"\0"))

        # Write Route Table: src(2), snk(2), orient(1), coupling(1)
        for r in routes:
            f.write(
                struct.pack(
                    "<HHbb", r["src_idx"], r["snk_idx"], r["orientation"], r["coupling"]
                )
            )

    print(f"Success! Fragment woven into {output_file} ({len(routes)} routes).")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python loomc.py <input.ls> <output.loom>")
    else:
        compile_loom(sys.argv[1], sys.argv[2])
