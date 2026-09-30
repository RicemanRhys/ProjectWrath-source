"""frndint rounds the way the x87 control word says (notes 18.50).

Imported by lift_flag_join.py, which lift.ps1 runs in place of `python -m tools.recomp`. Nothing in
xboxrecomp/ is edited: Lifter._lift_fpu is wrapped in this process only.

The gap. tools.recomp lifts `frndint` to C's rint(), which rounds with the host's rounding mode:
always to nearest. The x87 instruction rounds with the RC field of the guest's control word
(bits 10-11: 0 nearest, 1 down, 2 up, 3 toward zero), which the lifted code keeps in
g_fp_control_word (fldcw/fnstcw) and which fist/fistp already honour (recomp_fist).

The CRT's floor() and ceil() are built on exactly that: _ctrlfp loads a control word with RC down or
up (fldcw), then _frnd is `fld; frndint`. So in the lifted game floor() and ceil() rounded to nearest.

In Crash that breaks every animation. sub_00090BE0 turns an animation time into a key position:
block = floor((t - 1) / 32), local = t - 32 * block, key = floor(local) - 1, then a key byte and a
bit mask. With round-to-nearest, the second half of every 32-frame block of a longer animation
picked the next block, with a negative local time and key byte 255, and every animation whose time
had a fraction of one half or more got a key mask one key ahead. Crash, Coco and the enemies
snapped into other poses and sank, and the hub's pads left the floor, for half a block at a time.
"""
import tools.recomp.lifter as lifter

_lift_fpu = lifter.Lifter._lift_fpu
sites = set()

FRNDINT = ("{ double _v = fp_top(); unsigned _rc = (g_fp_control_word >> 10) & 3u;"
           " fp_top() = _rc == 1u ? floor(_v) : _rc == 2u ? ceil(_v) : _rc == 3u ? trunc(_v) : rint(_v); }"
           " /* frndint, by the control word's RC (lift_frndint.py) */")


def lift_fpu(self, insn, m, ops):
    if m == "frndint":
        sites.add(insn.address)
        return [FRNDINT]
    return _lift_fpu(self, insn, m, ops)


lifter.Lifter._lift_fpu = lift_fpu
