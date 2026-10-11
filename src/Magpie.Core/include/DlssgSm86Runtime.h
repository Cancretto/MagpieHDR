#pragma once
#include <dxgi.h>

namespace Magpie::DlssgSm86Runtime {

// Prepare before any NGX consumer. Failure disables community FG only.
// Once loaded, the proxy and its hooks live until process exit.
bool Prepare(IDXGIAdapter* adapter) noexcept;
// A byte identity check, NOT proof of kernel execution or image correctness.
bool VerifyLoadedPair() noexcept;

}
