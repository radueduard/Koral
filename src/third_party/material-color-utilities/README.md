# material-color-utilities (C++)

Google's Material Design colour library — HCT, tonal palettes, dynamic colour schemes, image quantisation —
from https://github.com/material-foundation/material-color-utilities (`cpp/`, commit 5b3618b, 2026-08-21),
under the Apache License 2.0 (`LICENSE`). Koral compiles it into its library, privately; `kmath/material.h`
is its interface.

Changed from upstream, so that it needs nothing but the standard library:
- `quantize/wsmeans.cc`: `absl::flat_hash_map` → `std::unordered_map`;
- `utils/utils.cc`: `absl::StrCat(absl::Hex(...))` → `snprintf("%x")`;
- `temperature/temperature_cache.h`: the `#include <optional>` it uses.
