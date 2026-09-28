"""Run tools.recomp with one more flag join: cmp/test snapshots of different widths (notes 18.43).

  python twoc/tools/lift_flag_join.py <tools.recomp arguments>

lift.ps1 runs this instead of `python -m tools.recomp`; the arguments are passed through unchanged.
Nothing in xboxrecomp/ is edited: the two functions below are wrapped in this process only.

The gap. When a jcc (or setcc/cmovcc) reads flags set in another basic block, the translator merges
the predecessors' flag states (translator._merge_flag_states). Two cmp (or two test) states merge even
when their operands differ, because every cmp/test snapshots its operands into _fa/_fb (masked to the
operand width) and _fas/_fbs (sign-extended) where it runs. But the merge also requires the same
operand width, and when the widths differ it gives up. The jcc then lifts to `if (_flags ...)`, and
_flags is a variable nothing assigns: the branch is never taken.

In Crash that makes the boss-script interpreter (sub_000120A0) treat its "hit" condition (type 0x12)
as always true. Its handler ends with `test al, al` and jumps to a `je` at 0x14D69 that the type 0x14
handler reaches after `test eax, eax`. So every phase of the water boss (Drain Damage) and the final
boss ends on its first frame, and the level ends a few seconds in.

The zero flag and the unsigned conditions don't depend on the width once the snapshot is masked:
TEST_Z(_fa, _fb) and CMP_EQ/CMP_B(_fa, _fb) are exact for whichever predecessor ran. So when the only
difference is the width, the merged state is marked "__snap_<kind>" and a consumer gets the ordinary
cmp/test condition for ZF and CF (je/jne, jb/jae/jbe/ja and their setcc/cmovcc forms). Everything that
reads SF or OF (js, jl, jg, ...) still gets no condition and keeps the old fallback.
"""
import os
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "xboxrecomp"))

import tools.recomp.lifter as lifter          # noqa: E402
import tools.recomp.translator as translator  # noqa: E402
from tools.recomp.__main__ import main        # noqa: E402

# Conditions that read only ZF and CF.
WIDTH_FREE = frozenset({"je", "jz", "jne", "jnz", "jb", "jnae", "jc", "jae", "jnb", "jnc",
                        "jbe", "jna", "ja", "jnbe"})
PREFIX = "__snap_"
_merge = translator._merge_flag_states
_condition = lifter._make_condition
joins = [0]


def _kind(state):
    k = state[0]
    return k[len(PREFIX):] if k.startswith(PREFIX) else k


def merge_flag_states(states):
    merged = _merge(states)
    if merged is not None or not states or any(not s or not s[0] for s in states):
        return merged
    kinds = {_kind(s) for s in states}
    if len(kinds) != 1 or kinds.pop() not in ("cmp", "test") or any(len(s[1]) != 2 for s in states):
        return None
    joins[0] += 1
    return (PREFIX + _kind(states[0]), list(states[0][1]))


def make_condition(jcc, flag_setter, flag_ops):
    if flag_setter and flag_setter.startswith(PREFIX):
        if jcc not in WIDTH_FREE:
            return None
        return _condition(jcc, flag_setter[len(PREFIX):], flag_ops)
    return _condition(jcc, flag_setter, flag_ops)


translator._merge_flag_states = merge_flag_states
lifter._make_condition = make_condition

if __name__ == "__main__":
    sys.argv[0] = "tools.recomp"
    try:
        main()
    finally:
        print("lift_flag_join: %d block join(s) merged across cmp/test widths" % joins[0])
