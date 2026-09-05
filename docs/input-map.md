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
