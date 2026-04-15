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
            if "%s" in r_name:
                r_name = r_name.replace("%s", "0")  # Simplify arrays

            r_offset_elem = register.find("addressOffset")
            if r_offset_elem is None:
                continue
            r_offset = int(r_offset_elem.text, 16)
            addr = base_addr + r_offset

            node_type = "GOOSE_CELL_VIRTUAL"
            if zone == "perception":
                node_type = "GOOSE_CELL_HARDWARE_IN"
            elif zone == "agency":
                node_type = "GOOSE_CELL_HARDWARE_OUT"
            elif zone in ["sys", "logic"]:
                node_type = "GOOSE_CELL_SYSTEM_ONLY"

            gname = f"{zone}.{p_name.lower()}.{r_name.lower()}"
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

            for field_xml in register.findall(".//fields/field"):
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

    shadow_nodes.sort(key=lambda x: x["name"])

    with open(output_path, "w") as f:
        f.write('#include "goose.h"\n')
        f.write("#include <string.h>\n\n")
        f.write(
            "/**\n * @brief THE ALL-SEEING ATLAS v2.0 (Automated Shadow Manifest)\n */\n\n"
        )

        # Self-contained struct definition to resolve build issues
        f.write("typedef struct {\n")
        f.write("    const char *name;\n")
        f.write("    uint32_t addr;\n")
        f.write("    uint32_t bit_mask;\n")
        f.write("    int8_t f, r, c;\n")
        f.write("    goose_cell_type_t type;\n")
        f.write("} shadow_node_t;\n\n")

        f.write("const shadow_node_t shadow_map[] = {\n")
        for n in shadow_nodes:
            f.write(
                f'    {{"{n["name"]}", 0x{n["addr"]:08X}, 0x{n["mask"]:08X}, {n["f"]}, {n["r"]}, {n["c"]}, {n["type"]}}},\n'
            )
        f.write("};\n\n")

        f.write(f"const size_t shadow_map_count = {len(shadow_nodes)};\n\n")
        f.write(
            "esp_err_t goose_shadow_resolve(const char *name, uint32_t *out_addr, uint32_t *out_mask, reflex_tryte9_t *out_coord, goose_cell_type_t *out_type) {\n"
        )
        f.write("    int low = 0, high = (int)shadow_map_count - 1;\n")
        f.write("    while (low <= high) {\n")
        f.write("        int mid = low + (high - low) / 2;\n")
        f.write("        int res = strcmp(name, shadow_map[mid].name);\n")
        f.write("        if (res == 0) {\n")
        f.write("            *out_addr = shadow_map[mid].addr;\n")
        f.write("            *out_mask = shadow_map[mid].bit_mask;\n")
        f.write(
            "            *out_coord = goose_make_coord(shadow_map[mid].f, shadow_map[mid].r, shadow_map[mid].c);\n"
        )
        f.write("            *out_type = shadow_map[mid].type;\n")
        f.write("            return ESP_OK;\n")
        f.write("        }\n")
        f.write("        if (res > 0) low = mid + 1;\n")
        f.write("        else high = mid - 1;\n")
        f.write("    }\n")
        f.write("    return ESP_ERR_NOT_FOUND;\n")
        f.write("}\n")


if __name__ == "__main__":
    scrape_svd(sys.argv[1], sys.argv[2], sys.argv[3])
