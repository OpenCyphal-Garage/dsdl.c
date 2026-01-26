# Tools

This directory contains helper tools for inspecting and validating the parser output.

## `dsdl_to_dsdl`

Emits canonicalized DSDL for the requested types.
Example:

```
./dsdl_to_dsdl -r test_dsdl_root_namespaces/0 -r test_dsdl_root_namespaces/1 -- uavcan.node.Heartbeat.1.0
```

## `dsdl_to_json`

Emits a JSON model for the requested types.
Example:

```
./dsdl_to_json -r test_dsdl_root_namespaces/0 -r test_dsdl_root_namespaces/1 -- mymsgs.Simple.1.0 > out.json
```

## `dsdl_compare_pydsdl.py`

Compares `dsdl_to_json` and `dsdl_to_dsdl` output against PyDSDL.
Example:

```
./dsdl_compare_pydsdl.py \
  -r test_dsdl_root_namespaces/0 \
  -r test_dsdl_root_namespaces/1 \
  --dsdl-to-json ./dsdl_to_json \
  --dsdl-to-dsdl ./dsdl_to_dsdl
```
