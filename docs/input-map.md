# Runtime input map

AYTHER Play can pass a TOML control map to Runtime with
`--input-map <path>`. Runtime reads and validates the file once during startup,
before SDL initialization, and stores the resolved SDL scancodes and gamepad
buttons used by the frame loop.

## Format

The top-level sections are `keyboard` and `gamepad`. A map may contain either
section and may override only some actions; omitted bindings retain Runtime's
defaults.

```toml
[keyboard]
up = "Up"
down = "Down"
left = "Left"
right = "Right"
b = "Z"
a = "X"
y = "A"
x = "S"
l = "Q"
r = "W"
start = "Return"
select = "Right Shift"

[gamepad]
b = "south"
a = "east"
y = "west"
x = "north"
l = "leftshoulder"
r = "rightshoulder"
start = "start"
select = "back"
```

Keyboard values use names accepted by `SDL_GetScancodeFromName`. The gamepad
contract deliberately uses physical face-button positions rather than printed
labels, so it is stable across controller layouts. Its accepted values are
`south`, `east`, `west`, `north`, `leftshoulder`, `rightshoulder`, `start`, and
`back`.

Gamepad directions are not configurable. Runtime always combines the D-pad and
left stick for `up`, `down`, `left`, and `right`, with an axis threshold of
16000. Keyboard, gamepad button, D-pad, and left-stick sources are OR-ed into
one RetroPad state.

## Validation

Runtime rejects the complete map when it contains:

- malformed TOML;
- a top-level section other than `keyboard` or `gamepad`;
- an unknown action or a non-string value;
- a keyboard or gamepad name outside the contracts above;
- a gamepad direction binding; or
- duplicate effective keyboard or gamepad bindings, including duplicates with
  inherited defaults.

On failure Runtime emits a `warning` status record with stable reason
`input.map_invalid`, writes a detailed diagnostic naming `--input-map` to
stderr, and exits with code `78`. This happens before SDL or Vulkan startup.

## Compatibility behavior

Without `--input-map`, Runtime uses the defaults shown above and keeps the
legacy `Backspace` alias for `select`. Supplying any valid input-map file,
including a partial one, disables that undocumented alias; only the effective
`select` binding remains active.

## Replay QA window

The visible replay of `ayther_audio_qa check --presentation visible` (spec 002) does not use
`--input-map`. The game is driven only by the recorded take, and no key, gamepad button or
gameplay shortcut reaches the game. The window reads just these keys:

| Key | Action |
| --- | --- |
| Space | Pause at the end of the frame in progress; in pause, resume from the next frame. |
| Left / Right | In pause, step one frame back or forward within the take (silent navigation). |
| `I` | Show or hide the debug record of the frame; never changes the playback or the position. |
| Page Up, Page Down, Home, End, mouse wheel | Scroll the visible debug record. |

A held key does not repeat. Without focus no key acts, and a key still held when the window
regains focus stays blocked until it is released. Left and Right together move nothing. Up
and Down never navigate. The debug record never takes Space, the arrows or `I`, and these keys
never reach the game. The full
behavior is described in `audio-qa-evidence.md` («Controles de inspección»).
