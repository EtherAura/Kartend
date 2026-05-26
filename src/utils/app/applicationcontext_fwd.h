#ifndef APPLICATIONCONTEXT_FWD_H
#define APPLICATIONCONTEXT_FWD_H

// Forward declaration of ApplicationContext for headers that only need to
// name the type (typically as `const ApplicationContext *ctx = nullptr`
// inside a setup struct or as a private member field). Including this
// header instead of the full applicationcontext.h saves the consumer
// the transitive cost of collectionutils.h + the Qt widget headers that
// the full ApplicationContext definition pulls in.
//
// Translation units that *dereference* ctx (call ctx->scrollManager(),
// read ctx->collection.collections, etc.) must still include the full
// applicationcontext.h. The fwd header is for declarations only.

struct ApplicationContext;

#endif // APPLICATIONCONTEXT_FWD_H
