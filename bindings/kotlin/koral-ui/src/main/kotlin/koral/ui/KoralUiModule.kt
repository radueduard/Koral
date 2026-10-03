package koral.ui

import koral.NativeModule
import koral.interop.Native

/** koral-ui's native module, loaded before the application starts so its hooks run. */
class KoralUiModule : NativeModule {
    override fun load() { Native.koralUi }
}
