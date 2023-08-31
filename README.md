# Habitat

A Secure Scuttlebutt client for Haiku.

![Habitat icon](./Habitat-icon.svg)

## Installing Dependencies

- Libsodium: `pkgman install devel:libsodium`
- ICU: `pkgman install icu66_devel`
- Catch2 (only needed for unit tests) `pkgman install catch2`

## Building

```
cd habitat
make
```

Once I have a functioning application I will add a build target to generate an
hpkg file.

## Unit tests

You can run unit tests with `make test`.