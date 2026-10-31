# The declarative binary grammar

`native/include/dbpfx/grammar.hpp` lets a binary layout be written once, as a C++ type. Lazy
readers, owned models and serializers are generated from that single declaration at compile time.
There is no hand-written `Parse()` / `UnParse()` pair to keep in sync.

## Declaring a format

```cpp
using namespace dbpfx::g;

using name_map_entry = seq<
    field<"instance", u64>,
    field<"length",   count_of<"name", u32>>,   // written from name.size()
    field<"name",     utf8<ref<"length">>>>;    // length taken from the field above

using name_map = seq<
    field<"version", u32>,
    field<"count",   count_of<"entries", u32>>,
    field<"entries", array<name_map_entry, ref<"count">>>>;
```

## Reading: lazy and zero-copy

```cpp
auto v = g::parse<name_map>(bytes);              // just stores two pointers
auto n = v.get<"count">();                       // reads 4 bytes
auto e = v.get<"entries">()[3];                  // walks to element 3
std::string_view name = e.get<"name">();         // points into `bytes`, no copy
```

* Offsets of fixed-size prefixes are folded at compile time:
  `static_assert(schema::dbpf_header::static_offset<...index_of<"index_position">()>() == 0x40)`.
* Variable-size fields are measured only when a later field is requested.
* `uniform<T>` marks a variable-layout element whose size is the same for every element of one array
  (the DBPF index entry: its size depends on the index flags, not on the entry). The stride is measured
  once and access becomes O(1).
* `ref<"name">` resolves through the chain of enclosing records and then the root context, so an index
  entry can see the `flags` of its index header, and the index can see `index_count` from the package
  header (passed as `root_ctx{{"index_count", n}}`).
* Views never own memory. Keep the underlying bytes (a `blob`, the package) alive while views exist.

## Writing

```cpp
g::model<name_map> m;                            // or v.own() to start from parsed data
g::model<name_map_entry> e;
e.set<"instance">(0x1234ull).set<"name">(std::string("llama_hat"));
m.get<"entries">().push_back(e);
byte_vec out = g::serialize<name_map>(m);        // "count" and "length" are filled in automatically
```

`serialize(parse(x).own()) == x` holds for every schema in `schema.hpp` (tested).

## Combinators

| combinator | meaning |
|---|---|
| `u8 u16 u32 u64 i32 f32` | little-endian scalars |
| `magic<"STBL">` | fixed signature, validated on read, emitted on write |
| `raw<N>` | N opaque bytes kept verbatim (reserved / unknown fields) |
| `count_of<"field", u32>` | integer written as the length of a sibling field |
| `computed<u32, Fn>` | integer written as `Fn::compute(model)` (sizes, checksums) |
| `utf8<Len>`, `utf16<Len>`, `bytes<Len>` | text/blobs; `Len` is `ref<"f">`, `lit<N>` or `rest` |
| `array<T, Len>` | sequence; random access when `T` is fixed-size or `uniform` |
| `when<"f", mask, T>` / `unless<...>` | field present only if `(f & mask) != 0` (or `== 0`) |
| `one_of<peek<off, T>, case_<v, A>, case_<w, B>>` | choose a layout by peeking at a discriminator |
| `seq<field<"name", T>...>` | record |

## Adding a resource format

1. Declare it in `schema.hpp` (or your own header).
2. Read it: `g::parse<my_format>(pkg.read(i).bytes())`.
3. Write it: `g::serialize<my_format>(model)` and `package_editor::replace`.
4. Add a round-trip test in `native/tests/tests.cpp`.

The real string-table schema shows most features in one place: two layouts chosen by `one_of`, UTF-16
vs UTF-8 text, a `count_of` count, and a `computed` total of string bytes (TS4).
