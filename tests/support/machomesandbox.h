#ifndef KARTEND_TESTS_SUPPORT_MACHOMESANDBOX_H
#define KARTEND_TESTS_SUPPORT_MACHOMESANDBOX_H

// Kartend-0ceoe: portable config sandbox for macOS test binaries.
//
// macOS Qt 6.8's QStandardPaths test mode reroutes by string-replacing the
// QDir::homePath() prefix (i.e. $HOME) with $HOME/.qttest in the
// NSSearchPathForDirectoriesInDomains result (qstandardpaths_mac.mm,
// writableLocation). Both sides must agree on the home for the replace to
// fire — and the two sides read DIFFERENT sources, each cached/queried
// differently. Empirically (macos-14 runner):
//
//  - Foundation (NSSearchPathForDirectoriesInDomains) IGNORES a mid-process
//    setenv("HOME") — CoreFoundation captures the user home before main()
//    runs (run 27276344261: locations stayed under /Users/runner). It DOES
//    honour CFFIXED_USER_HOME, which CF reads lazily (run 27209514389:
//    locations followed the fake home).
//  - QDir::homePath() reads $HOME per call (never CFFIXED_USER_HOME), so a
//    mid-process setenv("HOME") works for Qt.
//
// The two earlier single-variable attempts each fixed exactly one side
// (CFFIXED_USER_HOME: commit 48dd5c8, reverted; HOME: commit 14ecb79) and the
// sandbox guards correctly refused both times. Setting BOTH variables to the
// same fresh directory aligns Foundation and QDir::homePath(), so every
// QStandardPaths location lands under the fake home AND the test-mode .qttest
// replace fires — every existing "qttest" sandbox guard passes unchanged.
// The directory name itself also embeds "qttest" as belt-and-braces for any
// path resolved with test mode momentarily off (e.g. MainWindowFixture's
// escape-detection snapshot). If a future Qt/macOS change breaks either
// override, locations fall back to the real home WITHOUT the marker and the
// guards qFatal/QVERIFY-fail loudly instead of touching real config — the
// same fail-safe behaviour that caught both earlier attempts.
//
// Call FIRST in main(), before Q(Core)Application is constructed and before
// any QStandardPaths lookup. mkdtemp gives each test process its own home,
// so parallel ctest binaries can't collide (an isolation upgrade over the
// shared ~/.qttest on Linux). The directory is intentionally not cleaned up:
// HOME must stay valid for the whole process lifetime, CI runners are
// ephemeral, and it holds only a few KB of INI/cache files.
//
// No-op off macOS — Linux/Windows keep the stock test-mode sandbox.

// <QtGlobal>, not <QtSystemDetection>: the standalone QtSystemDetection
// convenience header is missing from Qt 6.4's packaging (CI's floor); QtGlobal
// provides the Q_OS_* macros on every supported Qt.
#include <QtGlobal>

#if defined(Q_OS_MACOS)
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h> // mkdtemp: POSIX puts it here on macOS (not <cstdlib> as on glibc)
#include <vector>
#endif

namespace KartendTest {

inline void installMacHomeSandbox() {
#if defined(Q_OS_MACOS)
  const char *tmp = ::getenv("TMPDIR");
  std::string templ = (tmp && *tmp) ? tmp : "/tmp";
  if (templ.back() != '/') {
    templ += '/';
  }
  templ += "kartend-qttest-home-XXXXXX";
  std::vector<char> buf(templ.begin(), templ.end());
  buf.push_back('\0');
  if (::mkdtemp(buf.data()) == nullptr) {
    // Refuse to run unsandboxed — the suites wipe/write config locations and
    // must never do that against the real user home.
    std::fprintf(stderr, "installMacHomeSandbox: mkdtemp(%s) failed — refusing to run tests "
                         "against the real HOME\n",
                 buf.data());
    std::abort();
  }
  // Both variables, same directory: CFFIXED_USER_HOME for Foundation (read
  // lazily by CF), HOME for QDir::homePath() (read per call by Qt). See the
  // header comment for why each alone is insufficient.
  ::setenv("CFFIXED_USER_HOME", buf.data(), 1);
  ::setenv("HOME", buf.data(), 1);
#endif
}

} // namespace KartendTest

#endif // KARTEND_TESTS_SUPPORT_MACHOMESANDBOX_H
