import {createContext,useContext} from 'react';

// Opt-in presentation scope only. Current DOM surfaces carry their own facts;
// neither this context nor overlay cleanup owns Preview geometry/visibility.
export const NativePreviewOverlayScope=createContext(false);
export function useNativePreviewOverlay(kind:'modal'|'floating'){
  return useContext(NativePreviewOverlayScope)?kind:undefined;
}
