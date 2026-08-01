#pragma once

#include "rtc/http/app.hpp"

namespace rtc::controllers {

// Terminal handler for the versioned API namespace.
//
// Exists for one structural reason: Crow resolves the route before any
// middleware runs, and a route miss short-circuits the request inside the
// connection layer. Without a rule that matches "/api/v<n>/<anything>", a call
// to an unsupported version (say /api/v9/auth/login) would 404 with a bare,
// header-less body and ApiVersionMiddleware would never get to explain why.
//
// Registering this catch-all gives the middleware a chance to run, which is
// where the actual version policy lives:
//
//   - unsupported version  -> ApiVersionMiddleware short-circuits with a
//                             machine-readable `unsupported_api_version` error
//                             and this handler is never invoked.
//   - supported version,
//     unknown path         -> falls through to this handler, which returns the
//                             project's canonical `not_found` envelope instead
//                             of Crow's default empty 404.
//
// Registration order matters and is the composition root's responsibility: Crow
// resolves ambiguity by lowest rule index, so this must be registered *after*
// every concrete route for those routes to win.
//
// Stateless by construction — it depends on nothing but the request path, so it
// is exposed as a static and needs no ownership in the composition root.
class ApiFallbackController {
  public:
    static void register_routes(http::App& app);
};

}  // namespace rtc::controllers
