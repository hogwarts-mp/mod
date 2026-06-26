#pragma once

#include "shared/modules/appearance.hpp"
// CCD appearance wire — receiver side. Reconstructs a CustomizableCharacterDefinition from a CcdProfile and
// LayerCCDOverTarget's it onto a proxy CCC so HL builds the full custom look (colour/swatch/crest) that
// runtime material-copy can't reach. Sender = AppearanceDump::BuildLocalCcd. Game thread only.

namespace HogwartsMP::Core::CcdWire {
    // Reconstruct the LOCAL player's CCD and LayerCCDOverTarget it onto the proxy CCC (UObjectBase* as void*
    // to keep SDK types out of the header). Used by the dev spawn preview (student_proxy.cpp).
    void MirrorLocalCcdToProxyCcc(void *proxyCcc);

    // Apply a RECEIVED CcdProfile to the proxy CCC — the per-player network path.
    void MirrorCcdToProxyCcc(void *proxyCcc, const HogwartsMP::Shared::Modules::CcdProfile &profile);

    // Drop the rooted-CCD bookkeeping for a destroyed proxy (call on despawn).
    void ForgetProxy(void *proxyCcc);
} // namespace HogwartsMP::Core::CcdWire
