include "Settings.lua"
include "Dependencies.lua"
include "Workspace.lua"

workspace "OpenBarnyard"
    OpenBarnyardWorkspaceSettings()

group "Engine"
    include "Toshi"
    include "PrimeDebug"
    include "OpenBarnyard"

group "Third-Party"
    include "Toshi/Vendor/minhook"
    include "Toshi/Vendor/Opcode"
