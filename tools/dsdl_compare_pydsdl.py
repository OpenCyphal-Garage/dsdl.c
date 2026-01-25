#!/usr/bin/env python3
import argparse
import difflib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

sys.setrecursionlimit(max(sys.getrecursionlimit(), 10000))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare dsdl_to_json/dsdl_to_dsdl output against PyDSDL.",
    )
    parser.add_argument(
        "-r",
        "--root",
        action="append",
        required=True,
        help="Root namespace directory (repeatable)",
    )
    parser.add_argument(
        "--dsdl-to-json",
        required=True,
        help="Path to dsdl_to_json executable",
    )
    parser.add_argument(
        "--dsdl-to-dsdl",
        required=True,
        help="Path to dsdl_to_dsdl executable",
    )
    parser.add_argument(
        "--invalid-root",
        action="append",
        default=["invalid"],
        help="Root namespace name treated as invalid (repeatable)",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Enable PyDSDL strict mode",
    )
    parser.add_argument(
        "--fail-fast",
        action="store_true",
        help="Stop on the first mismatch",
    )
    parser.add_argument(
        "--max-errors",
        type=int,
        default=20,
        help="Stop after reporting this many mismatches (default: 20)",
    )
    return parser.parse_args()


def normalize_roots(raw_roots: list[str]) -> list[Path]:
    roots: list[Path] = []
    seen: set[str] = set()
    for root in raw_roots:
        path = Path(root).resolve()
        key = str(path)
        if key in seen:
            continue
        seen.add(key)
        roots.append(path)
    return roots


def parse_type_name(namespace_parts: tuple[str, ...], filename: str) -> str:
    stem = Path(filename).stem
    stem_parts = stem.split(".")
    type_parts: list[str] | None = None
    if len(stem_parts) == 4 and stem_parts[0].isdigit():
        type_parts = stem_parts[1:]
    elif len(stem_parts) == 3:
        type_parts = stem_parts
    if type_parts is None or any(part == "" for part in type_parts):
        type_parts = [stem]
    return ".".join(list(namespace_parts) + type_parts)


def collect_dsdl_files(roots: list[Path]) -> list[tuple[Path, Path, tuple[str, ...], str]]:
    file_roots: dict[Path, Path] = {}
    for root in roots:
        if not root.is_dir():
            raise RuntimeError(f"Root is not a directory: {root}")
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            if path.suffix not in (".dsdl", ".uavcan"):
                continue
            resolved = path.resolve()
            prev_root = file_roots.get(resolved)
            if prev_root is None or len(root.parts) > len(prev_root.parts):
                file_roots[resolved] = root

    items: list[tuple[Path, Path, tuple[str, ...], str]] = []
    for path, root in file_roots.items():
        rel = path.relative_to(root)
        rel_no_ext = rel.with_suffix("")
        parts = rel_no_ext.parts
        namespace_parts = rel_no_ext.parent.parts
        type_name = parse_type_name(namespace_parts, rel.name)
        items.append((path, root, parts, type_name))

    items.sort(key=lambda x: x[3])
    return items


def type_name_key(full_name: str, major: int, minor: int) -> str:
    return f"{full_name}.{major}.{minor}"


def type_name_to_path(type_name: str) -> Path:
    parts = type_name.split(".")
    if len(parts) < 3:
        raise ValueError(f"Type name does not include version: {type_name}")
    short_name = parts[-3]
    major = parts[-2]
    minor = parts[-1]
    namespace_parts = parts[:-3]
    filename = f"{short_name}.{major}.{minor}.dsdl"
    return Path(*namespace_parts, filename)


def run_tool(exe: str, roots: list[Path], type_names: list[str]) -> subprocess.CompletedProcess:
    cmd = [exe]
    for root in roots:
        cmd.extend(["-r", str(root)])
    cmd.append("--")
    cmd.extend(type_names)
    return subprocess.run(cmd, capture_output=True, text=True)


def run_tool_single(exe: str, roots: list[Path], type_name: str) -> subprocess.CompletedProcess:
    return run_tool(exe, roots, [type_name])


def load_pydsdl() -> object:
    script_dir = Path(__file__).resolve().parent
    repo_root = script_dir.parent
    pydsdl_root = repo_root / "reference_implementations" / "pydsdl"
    sys.path.insert(0, str(pydsdl_root))
    import pydsdl  # type: ignore

    return pydsdl


def value_to_json(pydsdl: object, value: object) -> dict:
    if isinstance(value, pydsdl.Boolean):
        return {"kind": "bool", "value": bool(value.native_value)}
    if isinstance(value, pydsdl.Rational):
        num = value.native_value.numerator
        den = value.native_value.denominator
        return {"kind": "rational", "num": str(num), "den": str(den)}
    if isinstance(value, pydsdl.String):
        return {"kind": "string", "value": value.native_value}
    if isinstance(value, pydsdl.Set):
        elements = [value_to_json(pydsdl, elem) for elem in value]
        elements.sort(key=stable_json_key)
        return {"kind": "set", "elements": elements}
    if isinstance(value, pydsdl.SerializableType):
        name = value.full_name if isinstance(value, pydsdl.CompositeType) else ""
        return {"kind": "type", "name": name}
    raise TypeError(f"Unsupported value type: {type(value)}")


def type_to_json(pydsdl: object, data_type: object) -> dict:
    if isinstance(data_type, pydsdl.ArrayType):
        return {
            "kind": "array",
            "variable": isinstance(data_type, pydsdl.VariableLengthArrayType),
            "capacity": int(data_type.capacity),
            "element": type_to_json(pydsdl, data_type.element_type),
        }
    if isinstance(data_type, pydsdl.CompositeType):
        return {"kind": "composite", "name": data_type.full_name}
    if isinstance(data_type, pydsdl.BooleanType):
        return {"kind": "bool"}
    if isinstance(data_type, pydsdl.ByteType):
        return {"kind": "byte"}
    if isinstance(data_type, pydsdl.UTF8Type):
        return {"kind": "utf8"}
    if isinstance(data_type, pydsdl.VoidType):
        return {"kind": "void", "bits": int(data_type.bit_length)}
    if isinstance(data_type, pydsdl.SignedIntegerType):
        return {"kind": "int", "bits": int(data_type.bit_length), "truncated": False}
    if isinstance(data_type, pydsdl.UnsignedIntegerType):
        truncated = data_type.cast_mode == pydsdl.PrimitiveType.CastMode.TRUNCATED
        return {"kind": "uint", "bits": int(data_type.bit_length), "truncated": truncated}
    if isinstance(data_type, pydsdl.FloatType):
        truncated = data_type.cast_mode == pydsdl.PrimitiveType.CastMode.TRUNCATED
        return {"kind": "float", "bits": int(data_type.bit_length), "truncated": truncated}
    return {"kind": "unknown"}


def fields_to_json(pydsdl: object, comp: object) -> list[dict]:
    fields = []
    for field in comp.fields:
        entry = {
            "name": field.name,
            "type": type_to_json(pydsdl, field.data_type),
        }
        if entry["name"] == "" and entry["type"].get("kind") == "void":
            entry["padding"] = True
        fields.append(entry)
    return fields


def constants_to_json(pydsdl: object, comp: object) -> list[dict]:
    constants = []
    for const in comp.constants:
        constants.append(
            {
                "name": const.name,
                "type": type_to_json(pydsdl, const.data_type),
                "value": value_to_json(pydsdl, const.value),
            }
        )
    return constants


def composite_kind(pydsdl: object, comp: object) -> str:
    inner = comp.inner_type if hasattr(comp, "inner_type") else comp
    return "union" if isinstance(inner, pydsdl.UnionType) else "struct"


def composite_sealed(pydsdl: object, comp: object) -> bool:
    return not isinstance(comp, pydsdl.DelimitedType)


def section_to_json(pydsdl: object, comp: object, include_constants: bool) -> dict:
    sealed = composite_sealed(pydsdl, comp)
    extent_bits = int(comp.extent)
    extent_bytes = int(extent_bits // 8) if not sealed else 0
    section = {
        "kind": composite_kind(pydsdl, comp),
        "sealed": sealed,
        "extent_bytes": extent_bytes,
        "extent_bits": extent_bits,
        "fields": fields_to_json(pydsdl, comp),
    }
    if include_constants:
        section["constants"] = constants_to_json(pydsdl, comp)
    return section


def composite_to_json(pydsdl: object, comp: object) -> dict:
    out = {
        "name": comp.full_name,
        "version": [int(comp.version.major), int(comp.version.minor)],
        "deprecated": bool(comp.deprecated),
    }
    fixed_port_id = comp.fixed_port_id if comp.has_fixed_port_id else None

    if isinstance(comp, pydsdl.ServiceType):
        out["kind"] = "service"
        out["fixed_port_id"] = fixed_port_id
        out["request"] = section_to_json(pydsdl, comp.request_type, True)
        out["response"] = section_to_json(pydsdl, comp.response_type, False)
        return out

    sealed = composite_sealed(pydsdl, comp)
    extent_bits = int(comp.extent)
    extent_bytes = int(extent_bits // 8) if not sealed else 0
    out.update(
        {
            "kind": composite_kind(pydsdl, comp),
            "fixed_port_id": fixed_port_id,
            "sealed": sealed,
            "extent_bytes": extent_bytes,
            "extent_bits": extent_bits,
            "fields": fields_to_json(pydsdl, comp),
            "constants": constants_to_json(pydsdl, comp),
        }
    )
    return out


def stable_json_key(value: object) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def normalize_sets(value: object) -> object:
    if isinstance(value, dict):
        out = {}
        for key, item in value.items():
            out[key] = normalize_sets(item)
        if out.get("kind") == "set" and isinstance(out.get("elements"), list):
            elements = [normalize_sets(elem) for elem in out["elements"]]
            elements.sort(key=stable_json_key)
            out["elements"] = elements
        return out
    if isinstance(value, list):
        return [normalize_sets(elem) for elem in value]
    return value


def diff_json(expected: object, actual: object, label: str) -> str:
    expected_text = json.dumps(expected, sort_keys=True, indent=2)
    actual_text = json.dumps(actual, sort_keys=True, indent=2)
    diff = "\n".join(
        difflib.unified_diff(
            expected_text.splitlines(),
            actual_text.splitlines(),
            fromfile=f"expected:{label}",
            tofile=f"actual:{label}",
            lineterm="",
        )
    )
    return diff


def read_pydsdl_batch(
    pydsdl: object,
    files: list[Path],
    roots: list[Path],
    lookup_dirs: list[Path],
    strict: bool,
) -> dict[str, object]:
    direct, _ = pydsdl.read_files(
        files,
        roots,
        lookup_dirs,
        allow_unregulated_fixed_port_id=True,
        strict=strict,
    )
    mapping: dict[str, object] = {}
    for comp in direct:
        if getattr(comp, "has_parent_service", False):
            continue
        key = type_name_key(comp.full_name, comp.version.major, comp.version.minor)
        mapping[key] = comp
    return mapping


def read_pydsdl_single(
    pydsdl: object,
    path: Path,
    roots: list[Path],
    lookup_dirs: list[Path],
    strict: bool,
) -> object | None:
    try:
        direct, _ = pydsdl.read_files(
            [path],
            roots,
            lookup_dirs,
            allow_unregulated_fixed_port_id=True,
            strict=strict,
        )
    except Exception:
        return None
    candidates = [c for c in direct if not getattr(c, "has_parent_service", False)]
    if len(candidates) == 1:
        return candidates[0]
    resolved = path.resolve()
    for comp in candidates:
        if Path(comp.source_file_path).resolve() == resolved:
            return comp
    if candidates:
        return candidates[0]
    return None


def parse_dsdl_to_dsdl_output(output: str) -> dict[str, str]:
    blocks: list[list[str]] = []
    current: list[str] | None = None
    for line in output.splitlines():
        if line.strip() == "# dsdl_to_dsdl normalized output":
            if current:
                blocks.append(current)
            current = [line]
            continue
        if current is None:
            if line.strip() == "":
                continue
            raise RuntimeError("Unexpected dsdl_to_dsdl output before header")
        current.append(line)
    if current:
        blocks.append(current)

    result: dict[str, str] = {}
    for block in blocks:
        name = None
        for line in block:
            if line.startswith("# name: "):
                name = line[len("# name: ") :].strip()
                break
        if not name:
            raise RuntimeError("Failed to extract type name from dsdl_to_dsdl output")
        text = "\n".join(block) + "\n"
        result[name] = text
    return result


def derive_pydsdl_roots(items: list[tuple[Path, Path, tuple[str, ...], str]]) -> list[Path]:
    roots: list[Path] = []
    seen: set[str] = set()
    for _, base_root, parts, _ in items:
        if not parts:
            continue
        root = (base_root / parts[0]).resolve()
        key = str(root)
        if key in seen:
            continue
        seen.add(key)
        roots.append(root)
    return roots


def main() -> int:
    args = parse_args()
    roots = normalize_roots(args.root)
    invalid_roots = set(args.invalid_root)

    pydsdl = load_pydsdl()

    items = collect_dsdl_files(roots)
    if not items:
        print("No DSDL files found under provided roots", file=sys.stderr)
        return 1

    pydsdl_roots = derive_pydsdl_roots(items)

    invalid_items: list[tuple[Path, str]] = []
    valid_items: list[tuple[Path, str]] = []
    for path, root, parts, type_name in items:
        if parts and parts[0] in invalid_roots:
            invalid_items.append((path, type_name))
        else:
            valid_items.append((path, type_name))

    errors: list[str] = []

    pydsdl_map: dict[str, object] = {}
    valid_paths = [p for p, _ in valid_items]
    if valid_paths:
        try:
            pydsdl_map = read_pydsdl_batch(pydsdl, valid_paths, pydsdl_roots, pydsdl_roots, args.strict)
        except Exception as exc:
            print(f"PyDSDL batch parse failed ({exc}); falling back to per-file parsing", file=sys.stderr)
            pydsdl_map = {}
            for path, type_name in valid_items:
                comp = read_pydsdl_single(pydsdl, path, pydsdl_roots, pydsdl_roots, args.strict)
                if comp is None:
                    errors.append(f"PyDSDL failed to parse expected-valid type: {type_name}")
                    if args.fail_fast or len(errors) >= args.max_errors:
                        break
                    continue
                key = type_name_key(comp.full_name, comp.version.major, comp.version.minor)
                pydsdl_map[key] = comp

    # Validate invalid items using PyDSDL; promote to valid if PyDSDL accepts them.
    promoted: list[tuple[Path, str]] = []
    for path, type_name in invalid_items:
        comp = read_pydsdl_single(pydsdl, path, pydsdl_roots, pydsdl_roots, args.strict)
        if comp is None:
            continue
        promoted.append((path, type_name))
        key = type_name_key(comp.full_name, comp.version.major, comp.version.minor)
        pydsdl_map[key] = comp

    if promoted:
        invalid_items = [(p, t) for (p, t) in invalid_items if (p, t) not in promoted]
        valid_items.extend(promoted)
        valid_items.sort(key=lambda x: x[1])

    # Build expected JSON map from PyDSDL.
    pydsdl_json_map: dict[str, object] = {}
    for path, type_name in valid_items:
        comp = pydsdl_map.get(type_name)
        if comp is None:
            errors.append(f"PyDSDL missing type for file: {type_name}")
            if args.fail_fast or len(errors) >= args.max_errors:
                break
            continue
        pydsdl_json_map[type_name] = normalize_sets(composite_to_json(pydsdl, comp))

    # Run dsdl_to_json for valid types (batch, with fallback).
    dsdl_json_map: dict[str, object] = {}
    valid_type_names = [t for _, t in valid_items]
    if valid_type_names:
        result = run_tool(args.dsdl_to_json, roots, valid_type_names)
        if result.returncode == 0:
            try:
                parsed = json.loads(result.stdout)
            except json.JSONDecodeError as exc:
                errors.append(f"dsdl_to_json produced invalid JSON: {exc}")
                parsed = None
            if isinstance(parsed, list) and len(parsed) == len(valid_type_names):
                for name, obj in zip(valid_type_names, parsed):
                    dsdl_json_map[name] = normalize_sets(obj)
            else:
                errors.append("dsdl_to_json output shape mismatch; falling back to per-type runs")
        else:
            errors.append("dsdl_to_json failed for batch; falling back to per-type runs")

        if len(dsdl_json_map) != len(valid_type_names):
            dsdl_json_map = {}
            for name in valid_type_names:
                single = run_tool_single(args.dsdl_to_json, roots, name)
                if single.returncode != 0:
                    errors.append(f"dsdl_to_json failed for valid type: {name}")
                else:
                    try:
                        parsed = json.loads(single.stdout)
                    except json.JSONDecodeError as exc:
                        errors.append(f"dsdl_to_json invalid JSON for {name}: {exc}")
                        continue
                    if not isinstance(parsed, list) or len(parsed) != 1:
                        errors.append(f"dsdl_to_json output shape mismatch for {name}")
                        continue
                    dsdl_json_map[name] = normalize_sets(parsed[0])
                if args.fail_fast or len(errors) >= args.max_errors:
                    break

    # Compare dsdl_to_json vs PyDSDL.
    for name in valid_type_names:
        if name not in pydsdl_json_map or name not in dsdl_json_map:
            continue
        expected = pydsdl_json_map[name]
        actual = dsdl_json_map[name]
        if expected != actual:
            diff = diff_json(expected, actual, name)
            errors.append(f"dsdl_to_json mismatch for {name}\n{diff}")
            if args.fail_fast or len(errors) >= args.max_errors:
                break

    # dsdl_to_dsdl roundtrip via PyDSDL.
    dsdl_to_dsdl_output: dict[str, str] = {}
    if valid_type_names and (not args.fail_fast or len(errors) < args.max_errors):
        result = run_tool(args.dsdl_to_dsdl, roots, valid_type_names)
        if result.returncode == 0:
            try:
                dsdl_to_dsdl_output = parse_dsdl_to_dsdl_output(result.stdout)
            except Exception as exc:
                errors.append(f"Failed to parse dsdl_to_dsdl output: {exc}")
        else:
            errors.append("dsdl_to_dsdl failed for batch; falling back to per-type runs")

        if len(dsdl_to_dsdl_output) != len(valid_type_names):
            dsdl_to_dsdl_output = {}
            for name in valid_type_names:
                single = run_tool_single(args.dsdl_to_dsdl, roots, name)
                if single.returncode != 0:
                    errors.append(f"dsdl_to_dsdl failed for valid type: {name}")
                    if args.fail_fast or len(errors) >= args.max_errors:
                        break
                    continue
                try:
                    parsed = parse_dsdl_to_dsdl_output(single.stdout)
                except Exception as exc:
                    errors.append(f"Failed to parse dsdl_to_dsdl output for {name}: {exc}")
                    if args.fail_fast or len(errors) >= args.max_errors:
                        break
                    continue
                dsdl_to_dsdl_output.update(parsed)

    if dsdl_to_dsdl_output and (not args.fail_fast or len(errors) < args.max_errors):
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_root = Path(tmp_dir)
            dsdl_files = []
            tmp_roots: list[Path] = []
            tmp_seen: set[str] = set()
            for name, text in dsdl_to_dsdl_output.items():
                rel_path = type_name_to_path(name)
                out_path = tmp_root / rel_path
                out_path.parent.mkdir(parents=True, exist_ok=True)
                out_path.write_text(text, encoding="utf-8")
                dsdl_files.append(out_path)
                root_ns = name.split(".")[0]
                root_dir = (tmp_root / root_ns).resolve()
                key = str(root_dir)
                if key not in tmp_seen:
                    tmp_seen.add(key)
                    tmp_roots.append(root_dir)

            try:
                lookup_dirs = tmp_roots + pydsdl_roots
                roundtrip_map = read_pydsdl_batch(pydsdl, dsdl_files, tmp_roots, lookup_dirs, args.strict)
            except Exception as exc:
                errors.append(f"PyDSDL failed to parse dsdl_to_dsdl output: {exc}")
                roundtrip_map = {}

            for name in valid_type_names:
                comp = roundtrip_map.get(name)
                if comp is None:
                    errors.append(f"PyDSDL missing dsdl_to_dsdl type: {name}")
                    if args.fail_fast or len(errors) >= args.max_errors:
                        break
                    continue
                expected = pydsdl_json_map.get(name)
                if expected is None:
                    continue
                actual = normalize_sets(composite_to_json(pydsdl, comp))
                if expected != actual:
                    diff = diff_json(expected, actual, name)
                    errors.append(f"dsdl_to_dsdl mismatch for {name}\n{diff}")
                    if args.fail_fast or len(errors) >= args.max_errors:
                        break

    # Verify invalid types fail in dsdl_to_json and dsdl_to_dsdl.
    for path, name in invalid_items:
        res_json = run_tool_single(args.dsdl_to_json, roots, name)
        if res_json.returncode == 0:
            errors.append(f"dsdl_to_json succeeded for invalid type: {name}")
        res_dsdl = run_tool_single(args.dsdl_to_dsdl, roots, name)
        if res_dsdl.returncode == 0:
            errors.append(f"dsdl_to_dsdl succeeded for invalid type: {name}")
        if args.fail_fast or len(errors) >= args.max_errors:
            break

    if errors:
        for err in errors[: args.max_errors]:
            print(err, file=sys.stderr)
        if len(errors) > args.max_errors:
            print(f"... truncated; {len(errors)} total mismatches", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
