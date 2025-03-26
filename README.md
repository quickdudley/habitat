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

This project's primary home is on TildeGit at [jeremylist/habitat](https://tildegit.org/jeremylist/habitat) and I will likely notice pull requests and
issue reports sooner than on the other mirrors, but I will eventually notice them regardless of where they're placed. In order to reduce the likelihood of doubling up I recommend checking which branches exist on TildeGit before beginning a pull request.

The full list of mirrors that I actively maintain and monitor is as follows:

- [Codeberg](https://codeberg.org/jeremylist/habitat)
- [Gitlab](https://gitlab.com/quickdudley/habitat)

Please run `haiku-format -i {src,tests}/*.{h,cpp}` and commit any changes it
makes before submitting pull requests, and otherwise try to stay consistent with
the existing coding style. Where possible, use `BString` rather than
`std::string`, and use Haiku's messaging system for communication between
components. If you would like to contribute to the user interface, please
discuss it with me first as I may already have detailed plans for that feature.

You are also welcome to contribute financially via https://ko-fi.com/jeremylist

To anyone who contributes in any way: thank you so much!
