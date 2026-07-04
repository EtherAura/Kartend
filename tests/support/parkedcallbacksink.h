#pragma once

#include "metadatalookupprovider.h"

namespace KartendTest {

/// Fixture-owned home for a *parked* provider callback.
///
/// Some scraper test stubs PARK a production entity/media callback — they store
/// it instead of firing it — so a test can drive the async in-flight / pause /
/// cancel windows by hand. Those production callbacks capture the provider's
/// `shared_ptr` by value (deliberate lifetime management on the real path). If
/// a stub parks such a callback INTO one of its own members, the provider
/// transitively owns itself: a self-reference cycle that never reaches
/// refcount 0, so LSan flags every object the closure holds at process exit.
/// (That is exactly how the 2026-06-30 merge shipped red — a park-mode test
/// that forgot the manual break.)
///
/// This sink breaks the cycle *by construction*: the parked callback lives
/// here, in an object the test fixture owns — OUTSIDE the provider's ownership
/// subtree — so no self-cycle ever forms. The owning fixture drops the parked
/// callbacks from `cleanup()` (and, as a backstop, on destruction), releasing
/// the last strong ref to the provider. Stubs store into the sink instead of a
/// member, so there is no per-test clear to forget: a future park-mode test
/// cannot silently reintroduce the leak.
///
/// Only entity and media callbacks capture the provider `shared_ptr`; lookup
/// callbacks do not, so lookup parking stays a plain stub member and is not
/// mirrored here.
class ParkedCallbackSink {
public:
  MetadataLookupProvider::DetailCallback entity;
  MetadataLookupProvider::MediaCallback media;

  /// Drop every parked callback, releasing the provider `shared_ptr` each one
  /// holds. Idempotent; the owning fixture calls this from `cleanup()`.
  void clear() {
    entity = {};
    media = {};
  }

  ~ParkedCallbackSink() { clear(); }
};

} // namespace KartendTest
