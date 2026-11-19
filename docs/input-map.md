# Runtime input map

AYTHER Play can pass a TOML control map with `--input-map <path>`. Runtime
reads and validates the file once at startup, before SDL initialization, and
the frame loop uses the resolved SDL scancodes and gamepad buttons. The same
map drives both providers (libretro and `--provider tessera`).

## Format

The top-level sections are `keyboard` and `gamepad`. A map may contain either
section and may override only some actions; omitted bindings keep Runtime's
defaults. The example below shows the defaults.

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

- Keyboard values are names accepted by `SDL_GetScancodeFromName`.
- Gamepad values name physical face-button positions, not printed labels, so a
  map is stable across controller layouts. Accepted values: `south`, `east`,
  `west`, `north`, `leftshoulder`, `rightshoulder`, `start`, `back`.
- Gamepad directions are not configurable. Runtime always combines the D-pad
  and the left stick (axis threshold 16000) for `up`, `down`, `left` and
  `right`.
- Keyboard, gamepad buttons, D-pad and left stick are OR-ed into one RetroPad
  state.

## Validation

Runtime rejects the whole map when it contains:

- malformed TOML;
- a top-level section other than `keyboard` or `gamepad`;
- an unknown action or a non-string value;
- a keyboard or gamepad name outside the rules above;
- a gamepad direction binding; or
- duplicate effective keyboard or gamepad bindings, including duplicates with
  inherited defaults.

On failure Runtime emits a `warning` with reason `input.map_invalid`, writes a
diagnostic naming `--input-map` to stderr and exits `78`, before SDL or Vulkan
startup.

## Legacy Backspace alias

Without `--input-map`, `Backspace` is an additional `select` key. Any valid map,
even a partial one, disables that alias; only the effective `select` binding
remains. Independently of the map, holding `Backspace` during play rewinds.

## Replay QA window

The visible replay of `ayther_audio_qa check --presentation visible` does not
use `--input-map`. The recorded take (or pad timeline) is the only input; no
key, gamepad button or gameplay shortcut reaches the game. The window reads
only these keys:

| Key | Action |
| --- | --- |
| Space | Pause at the end of the frame in progress; in pause, resume from the next frame. |
| Left / Right | In pause, step one frame back or forward within the take (silent navigation). |
| `I` | Show or hide the debug record of the frame. Never changes playback or position. |
| Page Up, Page Down, Home, End, mouse wheel | Scroll the visible debug record. |
| Esc | Close the window (cancels the take before the natural end). |

A held key does not repeat. Without focus no key acts, and a key still held
when the window regains focus stays blocked until released. Left and Right
together move nothing. Up and Down never navigate. The debug record never takes
Space, the arrows or `I`, and these keys never reach the game.

Full behavior, including the status line and the natural end, is in
[audio-qa-evidence.md](audio-qa-evidence.md#inspection-controls).
