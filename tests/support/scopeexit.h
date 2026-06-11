#ifndef KARTEND_TESTS_SUPPORT_SCOPEEXIT_H
#define KARTEND_TESTS_SUPPORT_SCOPEEXIT_H

// Kartend-9o2a9: shared scope-guard for test teardown.
//
// Runs the given callable when the guard leaves scope — used by the
// QueryManager suites to stop their worker thread and delete the
// worker-affine QueryManager even when an assertion fails mid-test
// (QTest aborts the slot via return, so RAII is the only reliable
// teardown). Previously each suite carried a verbatim copy of this
// template; they now share this one.

#include <utility>

namespace KartendTest {

template <typename Func> class ScopeExit {
public:
  explicit ScopeExit(Func &&func) : m_func(std::forward<Func>(func)) {}
  ~ScopeExit() { m_func(); }

  ScopeExit(const ScopeExit &) = delete;
  ScopeExit &operator=(const ScopeExit &) = delete;

private:
  Func m_func;
};

template <typename Func> auto makeScopeExit(Func &&func) -> ScopeExit<Func> {
  return ScopeExit<Func>(std::forward<Func>(func));
}

} // namespace KartendTest

#endif // KARTEND_TESTS_SUPPORT_SCOPEEXIT_H
