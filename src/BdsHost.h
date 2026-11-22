#pragma once
// A BDS server loads exactly one entry point per DLL (ll_mod_load), but the
// terrain generator and the structure runtime are separate modules that each
// used to register themselves. The client port solved this by registering a
// single host and calling both modules; the BDS port needs the same seam, so
// both translation units expose their lifecycle through these functions
// instead of LL_REGISTER_MOD when KARMA_BDS_HOST is defined.
namespace shanhai_bds {
bool terrainLoad();
bool terrainEnable();
bool terrainDisable();
bool terrainUnload();

bool structuresLoad();
bool structuresEnable();
bool structuresDisable();
bool structuresUnload();
}
