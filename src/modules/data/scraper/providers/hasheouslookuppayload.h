#ifndef HASHEOUSLOOKUPPAYLOAD_H
#define HASHEOUSLOOKUPPAYLOAD_H

#include "errorutils.h"
#include "romhasher.h"

#include <QByteArray>

/// Pure, offline serialization of the JSON body Hasheous' Lookup/ByHash
/// endpoint accepts (Kartend-fgd5k). Only the body: the method, URL, query
/// and headers belong to the request builder, which is a later slice, as is
/// the MAME (component hash) variant.
namespace HasheousLookupPayload {

/// Serialize @p hashes as compact JSON with the case-sensitive properties
/// "MD5", "SHA1" and "CRC", one per non-empty input hash. Values are copied
/// verbatim (no case folding, trimming or recomputation); absent hashes are
/// omitted and `size` is never written. Fails with InvalidArgument when no
/// hash is present (an empty or size-only Result), since Hasheous can match
/// nothing without one.
///
/// Reentrant: reads only the const input and builds the returned bytes from
/// call-local state, so it is safe to call from any thread.
[[nodiscard]] ErrorUtils::Result<QByteArray> buildBody(const RomHasher::Result &hashes);

} // namespace HasheousLookupPayload

#endif // HASHEOUSLOOKUPPAYLOAD_H
