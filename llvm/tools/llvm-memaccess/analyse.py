#!/usr/bin/env python3.10
from __future__ import annotations

import argparse
import json
import sys
import traceback
from dataclasses import dataclass
from pprint import pprint
from sys import intern, stderr
from typing import Callable, Optional

DEBUG = False


@dataclass
class Field:
    name: str
    type: Optional[TypeInfo]
    array: bool
    offset: int
    size: int
    offset_bits: int
    size_bits: int
    reads: int
    writes: int

    def __init__(self, name, type, array, offset, size, offset_bits, size_bits):
        self.name = name
        self.type = type
        self.array = array
        self.offset = offset
        self.size = size
        self.offset_bits = offset_bits
        self.size_bits = size_bits
        self.reads = 0
        self.writes = 0


@dataclass
class TypeInfo:
    ident: str
    kind: str
    file: str
    dwarf_file: str
    line: Optional[int]
    odr: bool
    decl: bool
    size: Optional[int]
    reads: int
    writes: int
    fields: list[Field]

    def __str__(self):
        return self.ident

    def __init__(self, ident, kind, file="", line=None, odr=False, decl=False):
        self.ident = ident
        self.kind = kind
        self.file = file
        self.dwarf_file = None
        self.line = line
        self.odr = odr
        self.decl = decl
        self.size = None
        self.reads = 0
        self.writes = 0
        self.fields = []

    def merge_from(self, other):
        assert self.ident == other.ident
        self.kind = other.kind
        self.dwarf_file = other.dwarf_file
        self.file = other.file
        self.line = other.line
        self.odr = other.odr
        self.decl = other.decl
        if other.size is not None:
            self.size = other.size
        self.reads += other.reads
        self.writes += other.writes
        self.fields = other.fields

        other.ident = "$merged"
        other.field = "$merged"
        other.fields = []


def apply_op(type_info, kind, profile_count, offset, size):
    if offset < 0:
        size -= offset
        offset = 0
    assert size >= 1

    if kind == "load":
        type_info.reads += profile_count
    elif kind == "store":
        type_info.writes += profile_count
    else:
        raise Exception("invalid op")

    for f in type_info.fields:
        if offset >= f.offset + f.size or offset + size <= f.offset:
            continue
        if DEBUG:
            stderr.write(f" .. {type_info.ident}::{f.name}\n")
        if kind == "load":
            f.reads += profile_count
        elif kind == "store":
            f.writes += profile_count
        else:
            raise Exception("invalid op")
        if f.type is not None:
            apply_op(f.type, kind, profile_count, offset - f.offset, size)


def guess_type_size(info: TypeInfo) -> int:
    size = info.size
    if size is not None:
        return size
    size = 0
    for f in info.fields:
        if f.size == -1:
            f.size = guess_type_size(f.type)
        f_end = f.offset + f.size
        if f_end > size:
            size = f_end
    return size


def log_check_failure(message, info0, info1):
    if message:
        stderr.write(f"{message}\n")
    stderr.write("== Type A\n")
    pprint(info0, stderr)
    stderr.write("== Type B\n")
    pprint(info1, stderr)


def check_odr(info0, info1):
    failures = []
    size0 = info0.size
    size1 = info1.size
    if size0 is not None and size1 is not None and size0 != size1:
        failures.append("Type size mismatch")

    if info0.kind != info1.kind:
        failures.append("Type kind mismatch")

    fields0 = info0.fields
    fields1 = info1.fields
    if len(fields0) != len(fields1):
        failures.append("different number of fields")

    for i in range(0, min(len(fields0), len(fields1))):
        field0 = fields0[i]
        field1 = fields1[i]
        if (
            field0.name != field1.name
            or field0.type is not field1.type
            or field0.offset != field1.offset
            or (field0.size != field1.size and field0.size >= 0 and field1.size >= 0)
            or field0.offset_bits != field1.offset_bits
            or field0.size_bits != field1.size_bits
        ):
            failures.append(f"difference for field {i}: {field0.name}")
    if failures:
        stderr.write("= Type mismatch (ODR violation?)\n")
        for f in failures:
            stderr.write(f"  - {f}\n")
        log_check_failure("", info0, info1)


def compare_defs(info0, info1):
    mismatch = []
    if info0.odr != info1.odr:
        mismatch.append("is_odr")
    # These produce too much noise... They trigger for things like:
    #    - explicit template instantations used as location when available,
    #      otherwise class declaration is used.
    #    - thrift sometimes generates same class in multiple .h files
    #if info0.file != info1.file:
    #    mismatch.append("file")
    # if info0.line != info1.line:
    #    mismatch.append("line")
    if mismatch:
        log_check_failure(f"def mismatch {' '.join(mismatch)}", info0, info1)


def merge_types(types, info):
    ident = info.ident
    existing_type = types.get(ident)
    if existing_type is None:
        types[ident] = info
        return

    if info.kind == "placeholder" or info.decl:
        return

    if existing_type.kind == "placeholder":
        if info.kind != "placeholder":
            existing_type.merge_from(info)
        return

    if existing_type.decl:
        if not info.decl:
            existing_type.merge_from(info)
        return

    if info.decl:
        return

    # Both are definitions, check if they match...
    compare_defs(existing_type, info)
    if existing_type.odr and info.odr:
        check_odr(existing_type, info)
    existing_type.merge_from(info)


def read_types(types, filename, file_ident):
    with open(filename) as fp:
        data = json.load(fp)

    for t in data["dwarf_types"]:
        ident = t["ident"]
        if ident.startswith("L."):
            ident = file_ident + ident[len("L.") :]
        ident = intern(ident)

        file = intern(t.get("file", ""))
        line = t.get("line", None)
        odr = t["is_odr"]
        decl = t["is_decl"]
        tag_name = t.get("tag_name")
        if tag_name is None:
            kind = "?"
        elif tag_name == "DW_TAG_structure_type":
            kind = "struct"
        elif tag_name == "DW_TAG_class_type":
            kind = "class"
        elif tag_name == "DW_TAG_union_type":
            kind = "union"
        elif tag_name == "DW_TAG_enumeration_type":
            kind = "enum"
        else:
            kind = f"? {tag_name}"
        info = TypeInfo(ident, kind=kind, file=file, line=line, odr=odr, decl=decl)
        info.dwarf_file = data["ir_filename"]

        size_bits = t.get("size_bits")
        if size_bits is not None:
            if size_bits % 8 != 0:
                stderr.write(f"Warning: Ignoring size_bits {size_bits}\n")
            else:
                info.size = size_bits // 8

        for e in t["elements"]:
            # Ignore static members
            if e.get("is_static"):
                continue
            base_type = e["base_type"]
            base_type_info = types.get(base_type)
            if base_type_info is None:
                base_type_info = TypeInfo(
                    intern(base_type), kind="placeholder", decl=True
                )
                types[base_type] = base_type_info

            offset_bits = e["offset_bits"]
            size_bits = e.get("size_bits")
            offset = offset_bits // 8
            if size_bits is None:
                size = -1
            elif size_bits == 0:
                size = 0
            else:
                size = (7 + (offset_bits % 8) + size_bits) // 8
            is_array = e.get("is_array", False)
            field = Field(
                name=intern(e["name"]),
                type=base_type_info,
                array=is_array,
                offset=offset,
                size=size,
                offset_bits=offset_bits,
                size_bits=size_bits,
            )
            info.fields.append(field)
        merge_types(types, info)

    # postprocess to fill-in missing sizes for DW_TAG_inheritance
    for t in types.values():
        for f in t.fields:
            if f.size >= 0:
                continue
            if f.type is None:
                stderr.write(
                    f"Warning: No type for field of unknown size {f} in {t.ident}\n"
                )
                continue
            f.size = guess_type_size(f.type)


unknown = set()


def process_load_stores(types, filename, file_ident):
    with open(filename) as fp:
        data = json.load(fp)

    # TODO: split algo so we first read types from all files and then in phase 2
    # process all load/stores in all files.
    for op in data["load_stores"]:
        type_info = None
        offset = None
        aa_struct = op.get("aa_struct")
        if aa_struct is not None:
            type_name = aa_struct
            type_info = types.get(type_name)
            if type_info is None:
                if type_name not in unknown:
                    stderr.write(f"Unknown type: {type_name}\n")
                unknown.add(type_name)
            offset = op.get("aa_offset", 0)
        trace_type = op.get("trace_type")
        if trace_type is not None:
            if trace_type.startswith("L."):
                trace_type = file_ident + trace_type[len("L.") :]
            type_info_entry = types.get(trace_type)
            if type_info_entry is not None:
                # currently overwrite potential aa_struct results as gep is
                # likely better (TODO: but is it always better?)
                type_info = type_info_entry
                offset = op.get("trace_offset", 0)
        if type_info is None:
            continue
        assert offset is not None
        kind = op["kind"]
        size = op["size_bytes"]
        profile_count = op["profile_count"]
        if DEBUG:
            stderr.write(f"Applying {op['IR']}\n")
        apply_op(type_info, kind, profile_count, offset, size)


def expand_response_file_args(args: list[str]) -> list[str]:
    """Expand "response file" arguments where arguments are loaded from a file
    (`@xxx.argsfile`). This can help to avoid command line length limitations."""
    new_args = []
    for arg in args:
        if arg.startswith("@"):
            with open(arg[1:]) as fp:
                for line in fp:
                    if line and line[-1] == "\n":
                        line = line[:-1]
                    if line == "":
                        continue
                    new_args.append(line)
            continue
        new_args.append(arg)
    return new_args


def _shorten(string, prefix_max, suffix_max):
    if len(string) <= (prefix_max + suffix_max - 2):
        return string
    return f"{string[:prefix_max]}..{string[-suffix_max:]}"


def _do_per_file(
    description: str, filenames: list[str], function: Callable[[str], None]
) -> bool:
    """Run `function` on each file in the list. Abort if `function` returns False."""
    num_files = len(filenames)
    for idx, filename in enumerate(filenames):
        if stderr.isatty():
            stderr.write("\r")
        stderr.write(f"{description} {idx:4d}/{num_files} {_shorten(filename, 10, 40)}")
        if stderr.isatty():
            stderr.flush()
        else:
            stderr.write("\n")
        function(filename)
    if stderr.isatty():
        stderr.write("\r")
    stderr.write(f"{description} {num_files:4d}/{num_files} DONE   \n")


def main():
    types = {}

    parser = argparse.ArgumentParser()
    parser.add_argument("-o", "--out")
    parser.add_argument("filenames", metavar="FILE", nargs="+")

    args = parser.parse_args(expand_response_file_args(sys.argv[1:]))

    filenames = args.filenames
    file_idents = {}
    for index, filename in enumerate(filenames):
        file_idents[filename] = f"F{index}"

    import_failures = 0

    def do_read_types(filename):
        nonlocal import_failures
        try:
            read_types(types, filename, file_idents[filename])
        except Exception as e:
            traceback.print_exc()
            stderr.write(f"\n{filename}: error: {e}\n")
            import_failures += 1

    _do_per_file("read types", filenames, do_read_types)

    def do_process_load_stores(filename):
        try:
            process_load_stores(types, filename, file_idents[filename])
        except Exception as e:
            stderr.write(f"\n{filename}: error: {e}\n")

    _do_per_file("process load/stores", filenames, do_process_load_stores)

    typelist = list(types.values())
    typelist.sort(key=lambda x: x.reads + x.writes, reverse=True)

    # Show a quick summary on stdout immediately
    print()
    print("# Top 10")
    for t in typelist[0:5]:
        print()
        pprint(t, indent=2)

    out = args.out
    if out is not None:
        result_json = []
        for t in typelist:
            fields = []
            for f in t.fields:
                result_field = {
                    "name": f.name,
                    "type": f.type.ident,
                    "array": f.array,
                    "offset": f.offset,
                    "size": f.size,
                    "offset_bits": f.offset_bits,
                    "size_bits": f.size_bits,
                    "reads": f.reads,
                    "writes": f.writes,
                }
                fields.append(result_field)
            result_type = {
                "ident": t.ident,
                "kind": t.kind,
                "dwarf_file": t.dwarf_file,
                "file": t.file,
                "line": t.line,
                "odr": t.odr,
                "decl": t.decl,
                "reads": t.reads,
                "writes": t.writes,
                "fields": fields,
            }
            if t.size is not None:
                result_type["size"] = t.size
            result_json.append(result_type)
        with open(out, "w", encoding="utf-8") as fp:
            json.dump(result_json, fp)

    print(f"{import_failures}/{len(filenames)} failed to import.\n")


if __name__ == "__main__":
    main()
