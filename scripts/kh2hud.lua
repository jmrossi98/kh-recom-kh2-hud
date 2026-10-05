LUAGUI_NAME = "recom kh2 hud"
LUAGUI_AUTH = "jake"
LUAGUI_DESC = "KH2's status gauge and portrait in place of the game's own"

-- Steam build only. All addresses are relative to the exe base.
local BUILD_CHECK = 0x167B76   -- add [rbx+84],edx in the Steam exe

-- native half of the mod, built by build.bat
local DLL_PATH = "C:/Users/jakma/Documents/projects/kh-recom-kh2-hud/build/recom_kh2hud.dll"
local native_frame = nil

function _OnInit()
    if ReadByte(BUILD_CHECK) == 0x01 and ReadByte(BUILD_CHECK + 1) == 0x93 and ReadByte(BUILD_CHECK + 2) == 0x84 then
        local fn, err = package.loadlib(DLL_PATH, "rc_hud")
        if fn then
            native_frame = fn
            ConsolePrint("kh2 hud: native dll loaded")
        else
            ConsolePrint("kh2 hud: native dll not loaded: " .. tostring(err))
        end
    else
        ConsolePrint("kh2 hud: unknown build, script disabled")
    end
end

function _OnFrame()
    if native_frame then native_frame() end
end
