//! The Touch controllers as THUG2's pad (a PlayStation 2 pad), and what does what.
use engine_core::{Control, PadHeld, TouchState};
use thug2_sys as t2;

/// A grip or trigger counts as held past this pull.
const PRESSED: f32 = 0.5;

/// The game's controls on Touch (the runtime lists its own - its menu, placing the skater - after these).
pub const CONTROLS: &[Control] = &[
    Control::new("Either stick", "steer, push (up), trick directions"),
    Control::new("A / B", "ollie (X) / grind (triangle)"),
    Control::new("X / Y", "flip trick (square) / grab trick (circle)"),
    Control::new("Grips", "L1 / R1: spin left / right"),
    Control::new("Triggers", "L2 / R2: revert, nollie, switch"),
];

/// What the game is given of the controllers.
#[derive(Clone, Copy, Debug)]
pub struct Input {
    /// Its pad for the next steps.
    pub pad: t2::Pad,
    /// The player is busy elsewhere (a menu, building): the game waits.
    pub hold: bool,
}

impl Default for Input {
    fn default() -> Self {
        Self { pad: t2::PAD_AT_REST, hold: false }
    }
}

impl Input {
    /// This frame's controllers: the pad at rest while the runtime has them, or while the menu button is down (its
    /// chords are the runtime's).
    pub fn set_input(&mut self, touch: &TouchState, held: PadHeld) {
        self.hold = held.pause;
        self.pad = if held.held || touch.menu { t2::PAD_AT_REST } else { pad(touch) };
    }
}

/// The controllers as the game's pad: sticks 0-255 (0x80 at rest, up and left 0).
pub fn pad(t: &TouchState) -> t2::Pad {
    use t2::buttons::*;
    let axis = |v: f32| (128.0 + v.clamp(-1.0, 1.0) * 127.0).round() as u8;
    let mut buttons = 0;
    for (on, bit) in [
        // (laid out as a pad's: ollie and grind on the right face buttons, flip and grab on the left's, the grips
        // the shoulder buttons and the triggers the triggers)
        (t.a, CROSS),
        (t.b, TRIANGLE),
        (t.x, SQUARE),
        (t.y, CIRCLE),
        (t.left_grip > PRESSED, L1),
        (t.right_grip > PRESSED, R1),
        (t.left_trigger > PRESSED, L2),
        (t.right_trigger > PRESSED, R2),
    ] {
        if on {
            buttons |= bit;
        }
    }
    // Either stick is the game's direction stick (the pad's left; its right stick does nothing in the game): the
    // further pushed one, so a thumb can leave its stick for a button (the left one for flip and grab, X / Y).
    let length = |v: [f32; 2]| v[0] * v[0] + v[1] * v[1];
    let stick = if length(t.right_stick) > length(t.left_stick) { t.right_stick } else { t.left_stick };
    t2::Pad { left_x: axis(stick[0]), left_y: axis(-stick[1]), right_x: 0x80, right_y: 0x80, buttons }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn touch() -> TouchState {
        TouchState::default()
    }

    #[test]
    fn the_pad_is_the_games() {
        let mut t = touch();
        assert_eq!(pad(&t), t2::Pad { left_x: 128, left_y: 128, right_x: 128, right_y: 128, buttons: 0 });
        t.left_stick = [-1.0, 1.0];
        t.a = true;
        t.x = true;
        let p = pad(&t);
        assert_eq!((p.left_x, p.left_y), (1, 1), "left and up are low");
        assert_eq!(p.buttons, t2::buttons::CROSS | t2::buttons::SQUARE);
        // A pad's layout: B grind, Y grab, grips the shoulders, triggers the triggers.
        let mut u = TouchState { b: true, y: true, left_grip: 1.0, right_trigger: 1.0, ..touch() };
        assert_eq!(pad(&u).buttons, t2::buttons::TRIANGLE | t2::buttons::CIRCLE | t2::buttons::L1 | t2::buttons::R2);
        u = TouchState { right_grip: 1.0, left_trigger: 1.0, ..touch() };
        assert_eq!(pad(&u).buttons, t2::buttons::R1 | t2::buttons::L2);
        // The left thumb off its stick for X: the right stick steers.
        t.left_stick = [0.0, 0.0];
        t.right_stick = [1.0, -0.2];
        t.y = true;
        let p = pad(&t);
        assert_eq!((p.left_x, p.left_y), (255, 153), "the right stick as the direction");
        assert_eq!((p.right_x, p.right_y), (128, 128));
        assert_eq!(p.buttons & t2::buttons::CIRCLE, t2::buttons::CIRCLE);
        // Both pushed: the further one.
        t.left_stick = [-0.3, 0.0];
        assert_eq!(pad(&t).left_x, 255);
    }

    /// Touch to the game's pad, as `set_input` gives it: the pad at rest whenever the runtime has the controllers or
    /// the menu button is down, and the game waiting only while the player is busy elsewhere.
    #[test]
    fn touch_to_pad_and_neutral_while_held() {
        let pushed = TouchState { left_stick: [0.0, 1.0], a: true, right_grip: 1.0, ..touch() };
        let cases = [
            (pushed, PadHeld::default(), pad(&pushed), false),
            (pushed, PadHeld { held: true, pause: false }, t2::PAD_AT_REST, false),
            (pushed, PadHeld { held: true, pause: true }, t2::PAD_AT_REST, true),
            (TouchState { menu: true, ..pushed }, PadHeld::default(), t2::PAD_AT_REST, false),
            (touch(), PadHeld::default(), t2::PAD_AT_REST, false),
        ];
        for (i, (touch, held, want, hold)) in cases.into_iter().enumerate() {
            let mut input = Input::default();
            input.set_input(&touch, held);
            assert_eq!(input.pad, want, "case {i}");
            assert_eq!(input.hold, hold, "case {i}");
        }
        assert_eq!(pad(&pushed).left_y, 1, "pushed: up");
        assert_eq!(pad(&pushed).buttons, t2::buttons::CROSS | t2::buttons::R1);
    }
}
