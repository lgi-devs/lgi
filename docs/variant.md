# LGI Variant support

LGI provides extended overrides for supporting GLib's GVariant type.
it supports folloing operations with variants:

## Creation

Variants should be created using GLib.Variant(type, value)
constructor.  Type is either GLib.VariantType or just plain string
describing requested type of the variant.  Following types are supported:
    
- `b`, `y`, `n`, `q`, `i`, `u`, `q`, `t`, `s`, `d`, `o`, `g` are basic
  types, see either GVariant documentation or DBus specification
  for their meaning.  `value` argument is expected to contain
  appropriate string or number for the basic type.
- `v` is variant type, `value` should be another GLib.Variant instance.
- `m`type is 'maybe' type, `value` should be either `nil` or value
  acceptable for target type.
- `a`type is array of values of specified type, `value` is expected to
  contain Lua table (array) with values for the array.  If the array
  contains `nil` elements inside, it must contain also `n` field with
  the real length of the array.
- `(typelist)` is tuple of types, `value` is expected to contain
  Lua table (array) with values for the tuple members.
- `{key-value-pair}` is dictionary entry, `value` is expected to contain
  Lua table (array) with 2 values (key and value) for the entry.

There are two convenience exceptions from above rules:
- when array of dictionary entries is met (i.e. dictionary), `value`
  is expected to contain Lua table with keys and values mapping to
  dictionary keys and values
- when array of bytes is met, a bytestring is expected in the form of
  Lua string, not array of byte numbers.
  
Some examples creating valid variants follow:

    GLib = require('lgi').Glib
    local v1 = GLib.Variant('s', 'Hello')
    local v2 = GLib.Variant('d', 3.14)
    local v3 = GLib.Variant('ms', nil)
    local v4 = GLib.Variant('v', v3)
    local v5 = GLib.Variant('as', { 'Hello', 'world' })
    local v6 = GLib.Variant('ami', { 1, nil, 2, n = 3 })
    local v7 = GLib.Variant('(is)', { 100, 'title' })
    local v8 = GLib.Variant('a{sd}', { pi = 3.14, one = 1 })
    local v9 = GLib.Variant('aay', { 'bytetring1', 'bytestring2' })

## Data access

LGI implements following special properties for accessing data stored
inside variants

- `type` contains read-only string describing type of the variant
- `value` unpacks value of the variant. Simple scalar types are
  unpacked into their corresponding Lua variants, tuples and
  dictionary entries are unpacked into Lua tables (arrays), child
  varaints are expanded for `v`-typed variants.  Dictionaries return
  proxy table which can be indexed by dictionary keys to retrieve
  dictionary values.  Generic arrays are __not__ automatically
  expanded, the source variants are returned are returned instead.
- `# operator` Length operator is overriden for GLib.Variants,
  returning number of child elements.  Non-compound variants always
  return 0, maybe-s return 0 or 1, arrays, tuples and dictionary
  entries return number of children subvariants.
- `[number] operator` Compound variants can be indexed by number,
  returning n-th subvariant (array entry, n-th field of tuple etc).
- `pairs() and ipairs()` Variants support these methods, which behave
  as standard Lua enumerators.

Examples of extracting values from variants created above:

    assert(v1.type == 's' and v1.value == 'Hello')
    assert(v2.value == 3.14)
    assert(v3.value == nil and #v3 = 0)
    assert(v4.value == nil and #v4 = 1)
    assert(v5.value == v5 and #v5 == 2 and v5[2] == 'world')
    assert(#v6 == 3 and v6[2] == nil)
    assert(v7.value[1] == 100 and v7[1] == 100 and #v7 == 2)
    assert(v8.value.pi == 3.14 and v8.value['one'] == 1 and #v8 == 2)
    assert(v9[1] == 'bytestring1')
    for k, v in v8:pairs() do print(k, v) end

## Serialization

To serialize variant into bytestream form, use `data` property, which
return Lua string containing serialized variant.  Deserialization is
done by `Variant.new_from_data` constructor, which is similar to
`g_variant_new_from_data`, but it does _not_ accept `destroy_notify`
argument.  See following serialization example:

    local v = GLib.Variant('s', 'Hello')
    local serialized = v.data
    assert(type(data) == 'string')
    
    local newv = GLib.Variant.new_from_data(serialized, true)
    assert(newv.type == 's' and newv.value == 'Hello')

## Other operations

LGI also contains many of the original `g_variant_` APIs, but many of
them are not useful because their functionality is covered in more
Lua-native way by operations described above.  However, there there
are still some useful calls, which are enumerated here.  All of them
can be called using object notation on variant instances, e.g. `local
vt = variant:get_type()` See GLib documentation for their closer
description.

- `print(with_types)` returns textual format of the variant.  Note
  that LGI does not contain opposite operation, i.e. g_variant_parse
  is not implemented yet
- `is_of_type(type)` checks whether variant instance conforms to
  specified type
- `compare(other_variant)` and `equal(other_variant)` allow comparison
  of variant instances
- `byteswap()`, `is_normal_form()` and `get_normal_form()` for
  affecting the binary representation of variants.
- `get_type()` method returns `VariantType` instance representing type
  of the variant.  Seldom useful, `type` property returning type as
  string is usually better choice.
- `GLib.VariantBuilder` although builder is supported, it is seldom
  useful, because creation of variants using constructors above is
  usually preferred.  The exception may be creating of very large
  arrays, where creating source Lua table with source array might
  waste too much memory.  Building such array piece-by-piece using
  builder instance is preferred.  Note that VariantBuilder's `end()`
  method clashes with lua `end` keyword, so it is renamed to `_end()`.
- `VARIANT_TYPE_` constants are accessible as `GLib.VariantType.XXX`,
  e.g. `GLib.VariantType.STRING`.  Although there should not be many
  cases where these constants are needed.
