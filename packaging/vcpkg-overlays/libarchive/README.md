# libarchive vcpkg overlay

This port adapts Microsoft's vcpkg libarchive port at
`ca07b157e8b7f2a96f608f20b6fb08ff15ea50fb` (MIT; LICENSE.vcpkg.txt). It uses
the upstream libarchive 3.8.11 release archive with a pinned SHA-512. The
legacy bcrypt and Mbed TLS hunks are omitted for the Windows build. This
build uses OpenSSL for archive encryption.

Use the registry port again once the pinned registry supplies 3.8.11 or newer.
