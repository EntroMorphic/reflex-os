import xml.etree.ElementTree as ET
import json
import os
import sys


def scrape_svd(svd_path, zones_path, output_path):
    tree = ET.parse(svd_path)
    root = tree.getroot()

    with open(zones_path, "r") as f:
        zones = json.load(f)

    shadow_nodes = []
    zone_map = {
        "perception": -1,
        "sys": 0,
        "agency": 1,
        "comm": 2,
        "logic": 3,
        "radio": 4,
    }
    zone_counters = {z: 0 for z in zone_map}

    for peripheral in root.findall(".//peripheral"):
        p_name = peripheral.find("name").text
        base_addr = int(peripheral.find("baseAddress").text, 16)
        zone = zones.get(p_name, "sys")
        field = zone_map.get(zone, 0)
        region = zone_counters[zone]
        zone_counters[zone] += 1
        cell_idx = 0

        for register in peripheral.findall(".//registers/register"):
            # Basic Register Check
            r_name_elem = register.find("name")
            if r_name_elem is None:
                continue
            r_name = r_name_elem.text

            r_offset_elem = register.find("addressOffset")
            if r_offset_elem is None:
                continue
            r_offset = int(r_offset_elem.text, 16)

            node_type = "GOOSE_CELL_VIRTUAL"
            if zone == "perception":
                node_type = "GOOSE_CELL_HARDWARE_IN"
            elif zone == "agency":
                node_type = "GOOSE_CELL_HARDWARE_OUT"
            elif zone in ["sys", "logic"]:
                node_type = "GOOSE_CELL_SYSTEM_ONLY"
            elif zone == "comm":
                node_type = "GOOSE_CELL_HARDWARE_OUT"
            elif zone == "radio":
                node_type = "GOOSE_CELL_SYSTEM_ONLY"

            # Array expansion: SVD uses %s placeholder with <dim> and
            # <dimIncrement> to describe register arrays. Expand every
            # index so the full hardware surface is addressable by name.
            if "%s" in r_name:
                dim_elem = register.find("dim")
                dim_inc_elem = register.find("dimIncrement")
                dim = int(dim_elem.text) if dim_elem is not None else 1
                assert dim > 0, f"{p_name}.{r_name}: dim={dim} (invalid SVD)"
                dim_inc = (
                    int(dim_inc_elem.text, 16)
                    if dim_inc_elem is not None
                    else 4
                )
                indices = range(dim)
            else:
                indices = [None]
                dim_inc = 0

            fields_xml = register.findall(".//fields/field")

            for idx in indices:
                if idx is not None:
                    inst_name = r_name.replace("%s", str(idx))
                    addr = base_addr + r_offset + idx * dim_inc
                else:
                    inst_name = r_name
                    addr = base_addr + r_offset

                gname = f"{zone}.{p_name.lower()}.{inst_name.lower()}"
                shadow_nodes.append(
                    {
                        "name": gname,
                        "addr": addr,
                        "mask": 0xFFFFFFFF,
                        "f": field,
                        "r": region,
                        "c": cell_idx,
                        "type": node_type,
                    }
                )
                cell_idx += 1

                for field_xml in fields_xml:
                    f_name = field_xml.find("name").text
                    f_offset = int(field_xml.find("bitOffset").text)
                    f_width = int(field_xml.find("bitWidth").text)
                    mask = ((1 << f_width) - 1) << f_offset
                    fname = f"{gname}.{f_name.lower()}"
                    shadow_nodes.append(
                        {
                            "name": fname,
                            "addr": addr,
                            "mask": mask,
                            "f": field,
                            "r": region,
                            "c": cell_idx,
                            "type": node_type,
                        }
                    )
                    cell_idx += 1

    # Invariant: every name must be pure ASCII so Python's sort and
    # C's strcmp produce the same ordering. goose_shadow_resolve relies
    # on this for its binary-search correctness. An SVD or scraper
    # change that introduced a non-ASCII byte into a name would
    # silently break the resolver for entries past the divergence
    # point; assert loudly instead.
    for n in shadow_nodes:
        assert n["name"].isascii(), f"non-ASCII name in scrape output: {n['name']!r}"

    # Sort by raw bytes, not by Python's default Unicode ordering.
    # For pure ASCII the two are identical, but we make the invariant
    # explicit so a future non-ASCII regression fails at scrape time.
    shadow_nodes.sort(key=lambda x: x["name"].encode("ascii"))

    with open(output_path, "w") as f:
        f.write('#include "goose.h"\n')
        f.write("#include <string.h>\n\n")
        f.write(
            "/**\n * @brief THE ALL-SEEING ATLAS v3.0 (Automated Shadow Manifest)\n *\n"
            " * Generated by tools/goose_scraper.py from tools/esp32c6.svd.\n"
            " * Array registers (SVD <dim>/<dimIncrement>) are fully expanded so\n"
            " * every channel, pin, and memory bank index is individually addressable.\n"
            " * The shadow_node_t type, shadow_map / shadow_map_count declarations,\n"
            " * and goose_shadow_resolve prototype all live in components/goose/include/goose.h\n"
            " * so other modules can walk the catalog directly.\n"
            " *\n"
            " * NOTE: the positional initializer order below must match the field\n"
            " * order of shadow_node_t in components/goose/include/goose.h. If you\n"
            " * reorder one, regenerate the other — the build will break if they drift.\n"
            " */\n\n"
        )

        f.write("const shadow_node_t shadow_map[] = {\n")
        for n in shadow_nodes:
            # Positional initializer order: name, addr, bit_mask, f, r, c, type.
            # Must match shadow_node_t in components/goose/include/goose.h.
            f.write(
                f'    {{"{n["name"]}", 0x{n["addr"]:08X}, 0x{n["mask"]:08X}, {n["f"]}, {n["r"]}, {n["c"]}, {n["type"]}}},\n'
            )
        f.write("};\n\n")

        f.write(f"const size_t shadow_map_count = {len(shadow_nodes)};\n\n")
        f.write(
            "reflex_err_t goose_shadow_resolve(const char *name, uint32_t *out_addr, uint32_t *out_mask, reflex_tryte9_t *out_coord, goose_cell_type_t *out_type) {\n"
        )
        f.write("    int low = 0, high = (int)shadow_map_count - 1;\n")
        f.write("    while (low <= high) {\n")
        f.write("        int mid = low + (high - low) / 2;\n")
        f.write("        int res = strcmp(name, shadow_map[mid].name);\n")
        f.write("        if (res == 0) {\n")
        f.write("            *out_addr = shadow_map[mid].addr;\n")
        f.write("            *out_mask = shadow_map[mid].bit_mask;\n")
        f.write(
            "            *out_coord = goose_make_shadow_coord(shadow_map[mid].f, shadow_map[mid].r, shadow_map[mid].c);\n"
        )
        f.write("            *out_type = shadow_map[mid].type;\n")
        f.write("            return REFLEX_OK;\n")
        f.write("        }\n")
        f.write("        if (res > 0) low = mid + 1;\n")
        f.write("        else high = mid - 1;\n")
        f.write("    }\n")
        f.write("    return REFLEX_ERR_NOT_FOUND;\n")
        f.write("}\n")


if __name__ == "__main__":
    scrape_svd(sys.argv[1], sys.argv[2], sys.argv[3])
