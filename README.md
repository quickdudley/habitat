# Habitat

A Secure Scuttlebutt client for Haiku.

## Installing Dependencies

- Libsodium: `pkgman install devel:libsodium`

## Building

Unfortunately pijul has dependencies which do not build on Haiku at present, so
you will need to use another operating system to obtain the source code, etc.

Once you have the source code on a BeFS volume mounted in Haiku the build
procedure is as follows

```
cd habitat
make
```

Once I have a functioning application I will add a build target to generate an
hpkg file.
