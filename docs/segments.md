# Segment Naming

`SEGMENT_NAME` assigns a stable logical prefix to a plugin's generated code and
writable data segments:

```cpp
#define SEGMENT_NAME gameplay
```

## Length Limits

The framework stores logical segment names without the platform prefix.

| Target | Code segment | Writable data segment |
| --- | --- | --- |
| Mach-O | `__` + up to 14 logical characters | `__` + logical name with `_data` suffix, up to 14 logical characters total |
| ELF | `.` + the logical name | `.` + logical name with `_data` suffix |

Mach-O segment names have a fixed 16-byte field. Since ArmCaveHook reserves the
first two bytes for the `__` prefix, a logical code segment name is limited to
14 ASCII characters. A writable data segment appends `_data`, so its base name
can retain at most 9 characters.

For example:

```cpp
#define SEGMENT_NAME arcratingpatch
```

produces the Mach-O code segment `__arcratingpatch`. Its data segment cannot be
named `__arcratingpatch_data`: that name is 21 characters including the `__`
prefix and exceeds the 16-byte Mach-O limit. The generated data segment is
`__arcrating_data` instead.

Use a `SEGMENT_NAME` of at most 9 characters when the data segment must retain
the complete logical prefix before `_data`.

## Collisions

Names are normalized to lowercase ASCII letters, digits, and underscores. When
two plugins request the same code or data name, ArmCaveHook appends a stable
short hash suffix. It reports an error if uniqueness still cannot be achieved.

## Shared cave names

ArmCaveHook does not open one segment per plugin. It packs every plugin code
section of a patch run into a single R+X cave and every writable data section
into a single R+W cave (see the segment layout section of the README). The cave
name is derived from the **longest common prefix** of the plugin segment names:

1. take the longest common prefix of all plugin segment names and strip the
   platform prefix (`__` / `.`);
2. append `_` if the remaining prefix does not end with one;
3. append `code` / `data`.

| Plugin segment names | Cave names |
| --- | --- |
| `__ncp_ap`, `__ncp_cp`, `__ncp_p64`, `__ncp_rs` | `__ncp_code` / `__ncp_data` |
| a single plugin `__ncp_rs` | `__ncp_rs_code` / `__ncp_rs_data` |
| nothing in common (e.g. `__ncp_rs` and `__zzzplay`) | falls back to `__armcave_code` / `__armcave_data` |

The Mach-O segment name field is 16 bytes, so the logical prefix is capped at
9 characters and truncated beyond that. An Android (ELF) `PT_LOAD` has no name
field, so caves are split by the same rule but have no name to show.

