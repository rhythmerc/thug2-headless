// The SDL3 calls the decomp's core makes (its SDL input source, UI backend and window helpers), headless: no window,
// no events, no gamepads, no keys down, the cursor nowhere. The host gives the pad (backends/scripted_input.cpp), so
// nothing here is ever the game's input. Built against SDL3's headers only: no SDL library is linked (on Android SDL
// would want its own activity).
#include <SDL3/SDL.h>

#include <array>
#include <cstdlib>

extern "C" {

bool SDL_Init(SDL_InitFlags) { return true; }
void SDL_free(void* memory) { std::free(memory); }
bool SDL_PollEvent(SDL_Event*) { return false; }

const bool* SDL_GetKeyboardState(int* numkeys)
{
    static std::array<bool, SDL_SCANCODE_COUNT> keys{};
    if (numkeys) *numkeys = SDL_SCANCODE_COUNT;
    return keys.data();
}
SDL_Window* SDL_GetKeyboardFocus() { return nullptr; }
SDL_Scancode SDL_GetScancodeFromName(const char*) { return SDL_SCANCODE_UNKNOWN; }
const char* SDL_GetScancodeName(SDL_Scancode) { return ""; }

SDL_MouseButtonFlags SDL_GetMouseState(float* x, float* y)
{
    if (x) *x = 0.0F;
    if (y) *y = 0.0F;
    return 0;
}
SDL_MouseButtonFlags SDL_GetGlobalMouseState(float* x, float* y) { return SDL_GetMouseState(x, y); }
SDL_Window* SDL_GetMouseFocus() { return nullptr; }
bool SDL_CursorVisible() { return false; }
bool SDL_HideCursor() { return true; }
bool SDL_ShowCursor() { return true; }
bool SDL_SetWindowRelativeMouseMode(SDL_Window*, bool) { return true; }

SDL_Window* SDL_GetWindowFromEvent(const SDL_Event*) { return nullptr; }
SDL_Window* SDL_GetWindowFromID(SDL_WindowID) { return nullptr; }
bool SDL_GetWindowPosition(SDL_Window*, int* x, int* y)
{
    if (x) *x = 0;
    if (y) *y = 0;
    return false;
}
bool SDL_GetWindowSize(SDL_Window*, int* w, int* h)
{
    if (w) *w = 640;
    if (h) *h = 480;
    return false;
}

int SDL_AddGamepadMappingsFromFile(const char*) { return 0; }
SDL_JoystickID* SDL_GetGamepads(int* count)
{
    if (count) *count = 0;
    return nullptr;
}
bool SDL_IsGamepad(SDL_JoystickID) { return false; }
SDL_Gamepad* SDL_OpenGamepad(SDL_JoystickID) { return nullptr; }
void SDL_CloseGamepad(SDL_Gamepad*) {}
SDL_Gamepad* SDL_GetGamepadFromID(SDL_JoystickID) { return nullptr; }
bool SDL_GamepadConnected(SDL_Gamepad*) { return false; }
Sint16 SDL_GetGamepadAxis(SDL_Gamepad*, SDL_GamepadAxis) { return 0; }
bool SDL_GetGamepadButton(SDL_Gamepad*, SDL_GamepadButton) { return false; }
SDL_Joystick* SDL_GetGamepadJoystick(SDL_Gamepad*) { return nullptr; }
const char* SDL_GetGamepadName(SDL_Gamepad*) { return nullptr; }
const char* SDL_GetGamepadNameForID(SDL_JoystickID) { return nullptr; }
int SDL_GetGamepadPlayerIndex(SDL_Gamepad*) { return -1; }
bool SDL_SetGamepadPlayerIndex(SDL_Gamepad*, int) { return false; }
const char* SDL_GetJoystickNameForID(SDL_JoystickID) { return nullptr; }
bool SDL_RumbleGamepad(SDL_Gamepad*, Uint16, Uint16, Uint32) { return false; }
bool SDL_RumbleJoystick(SDL_Joystick*, Uint16, Uint16, Uint32) { return false; }

} // extern "C"
