# Vendored third-party code

## doctest.h

- **Version:** 2.4.11 (single-header release)
- **Upstream:** https://github.com/doctest/doctest
  (fetched from `raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h`)
- **SHA-256:** `44faa038e9c3f9728efbda143748d01124ea0a27f4bf78f35a15d8fab2e039fb`
- **License:** MIT (`LICENSE.txt` alongside, fetched from the same tag)
- **Policy:** never edit the vendored header. To upgrade, fetch the new tag,
  record the new version + checksum here, and run `make test`.

Assignment-compliance note: the course rules ban compiler-construction tools
(flex, antlr, regex-based parsers) — a unit-test framework replaces nothing the
project is about and is fine.
