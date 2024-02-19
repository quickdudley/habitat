# Habitat

A Secure Scuttlebutt client for Haiku.

![Habitat icon](./Habitat-icon.svg)

## Installing Dependencies

- Libsodium: `pkgman install devel:libsodium`
- ICU: `pkgman install icu66_devel`
- Sqlite: `pkgman install sqlite_devel`

## Building

```
cd habitat
make
```

Once I have a fully functioning application I will add a build target to
generate an hpkg file; and will also submit releases to [HaikuPorts](https://github.com/haikuports/haikuports/wiki/)

## Unit tests

Unit tests have two additional dependencies which can be installed with
`pkgman install catch2_devel vim`

You can run unit tests with `make test`.

## Contributing

This project's primary home is on GitLab at [quickdudley/habitat](https://gitlab.com/quickdudley/habitat) and I will likely notice pull requests and
issue reports sooner than on the GitHub mirror. Furthermore, before starting a
pull request I advise checking GitLab for any related branches as GitHub only
has `main`. With those said: feel free to submit pull requests and issue reports
to either.

Please run `haiku-format -i {src,tests}/*.{h,cpp}` and commit any changes it
makes before submitting pull requests, and otherwise try to stay consistent with
the existing coding style. Where possible, use `BString` rather than
`std::string`, and use Haiku's messaging system for communication between
components. If you would like to contribute to the user interface, please
discuss it with me first as I may already have detailed plans for that feature.

You are also welcome to contribute financially via https://ko-fi.com/jeremylist

To anyone who contributes in any way: thank you so much!
