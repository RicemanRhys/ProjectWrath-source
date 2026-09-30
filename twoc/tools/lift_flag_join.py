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

Mixed setters (notes 18.44). When the predecessors disagree on the setter itself (cmp on one edge,
test or dec on another), no single expression answers the jcc. Each predecessor then evaluates the
conditions its own state can answer at its end, just before its jump, into the bit set _fj, and the
join's consumer reads its bit:

  bit 0 je   bit 1 jb   bit 2 jbe   bit 3 jl   bit 4 jle   bit 5 js   bit 6 jo   bit 7 jp

(the negated forms read the same bit inverted). A bit is usable at the join only when every
predecessor could answer it; otherwise that consumer keeps the old fallback. The merged state is
"__mat_<mask>", so it passes through blocks that set no flags and merges again further on. The join
is found while the function is lifted, after its earlier predecessors may already be emitted, so a
function with a new mixed join is lifted a second time. _fj is its own variable: the _flags fallback
must stay 0, or every other fallback branch would start being taken.

In Crash: the boss-script interpreter's distance and float conditions (0x14D08, 0x13749), the player
update's jge (0x62500), and a handful of others.
"""
import copy
import os
import re
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "xboxrecomp"))

import tools.recomp.lifter as lifter          # noqa: E402
import tools.recomp.translator as translator  # noqa: E402
from tools.recomp.__main__ import main        # noqa: E402
import lift_frndint                           # noqa: E402  frndint by the control word's RC (notes 18.50)

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


MAT = "__mat_"
# (bit, the conditions that read it as set, the conditions that read it as clear)
BITS = ((0, ("je", "jz"), ("jne", "jnz")),
        (1, ("jb", "jnae", "jc"), ("jae", "jnb", "jnc")),
        (2, ("jbe", "jna"), ("ja", "jnbe")),
        (3, ("jl", "jnge"), ("jge", "jnl")),
        (4, ("jle", "jng"), ("jg", "jnle")),
        (5, ("js",), ("jns",)),
        (6, ("jo",), ("jno",)),
        (7, ("jp", "jpe"), ("jnp", "jpo")))
BIT_OF = {j: (b, True) for b, pos, _ in BITS for j in pos}
BIT_OF.update({j: (b, False) for b, _, neg in BITS for j in neg})
# Per function start, from its previous lift: the predecessor blocks that write _fj, the bits the
# consumers read, and the joins (by predecessor set) not to make. Each lift records them per block:
# the translator's back-edge probe lifts a block several times with states that haven't settled, and
# only the last lift is emitted.
need = {}
wanted = {}
refused = {}
joins_now = {}      # (function, block) -> (predecessors that write _fj, {predecessor: assumed state},
                    #                       origin, origins of the joined states it merges)
reads_now = {}      # (function, block) -> {(bit, origin)} read
outs_now = {}       # (function, block) -> out-state
current = [None, None]  # the block being lifted (function, start); its join, as in joins_now
mixed = [0]
_incoming = translator._incoming_flag_state
_lift_block = translator.lift_basic_block
_translate = translator.FunctionTranslator.translate_function


def _mat(kind):
    """(mask, origin) of a joined state. The origin names the join's predecessors, so joined states
    from different joins stay distinct and a join whose result nothing reads writes nothing."""
    mask, origin = kind[len(MAT):].split("_", 1)
    return int(mask, 16), origin


def make_condition(jcc, flag_setter, flag_ops):
    if flag_setter and flag_setter.startswith(MAT):
        bit = BIT_OF.get(jcc)
        mask, origin = _mat(flag_setter)
        if bit is None or not (mask >> bit[0]) & 1:
            return None
        reads_now.setdefault(current[0], set()).add((bit[0], origin))
        return ("(_fj >> %d & 1)" % bit[0] if bit[1] else "!(_fj >> %d & 1)" % bit[0]), "joined flags"
    if flag_setter and flag_setter.startswith(PREFIX):
        if jcc not in WIDTH_FREE:
            return None
        return _condition(jcc, flag_setter[len(PREFIX):], flag_ops)
    return _condition(jcc, flag_setter, flag_ops)


def state_bits(state):
    """{bit: C expression} for what this flag state can answer.

    An answer that reads _cf counts only where the function declares it (the translator decides that
    from the instruction stream, before any join is seen).
    """
    out = {}
    for b, pos, neg in BITS:
        r = make_condition(pos[0], state[0], state[1])
        if r is not None:
            expr = r[0]
        else:
            r = make_condition(neg[0], state[0], state[1])
            if r is None:
                continue
            expr = "!(%s)" % r[0]
        if translator_self[0].lifter.needs_cf or not re.search(r"\b_cf\b", expr):
            out[b] = expr
    return out


def incoming_flag_state(sources, known, is_entry):
    merged = _incoming(sources, known, is_entry)
    if merged is not None or is_entry or not sources or not all(p in known for p in sources):
        return merged
    states = [known[p] for p in sources]
    if any(not s or not s[0] for s in states):
        return None
    mask = 0xFF
    ups = set()
    for s in states:
        if s[0].startswith(MAT):
            mask &= _mat(s[0])[0]
            ups.add(_mat(s[0])[1])
        else:
            mask &= sum(1 << b for b in state_bits(s))
    if not mask or frozenset(sources) in refused.get(translator_self[0].lifter.func_start, ()):
        return None
    origin = ".".join("%X" % p for p in sorted(sources))
    current[1] = (frozenset(p for p in sources if not known[p][0].startswith(MAT)),
                  {p: known[p] for p in sources}, origin, ups)
    return (MAT + "%02X_%s" % (mask, origin), [])


def _jump_index(lifter_, bb, flag_state, stmts):
    """Where the block's own jump starts in stmts (len(stmts) when it falls through)."""
    insns = bb.instructions
    last = insns[-1]
    if not (last.is_cond_jump or last.mnemonic in ("jmp", "loop", "loope", "loopne", "jecxz", "jcxz")):
        return len(stmts)
    fused = len(insns) >= 2 and lifter.try_match_cmp_jcc(insns, len(insns) - 2, lifter=lifter_)
    head = copy.copy(bb)
    head.instructions = insns[:-2] if fused else insns[:-1]
    saved = copy.deepcopy(lifter_.unimplemented)
    prefix = _lift_block(lifter_, head, flag_state=flag_state)[0] if head.instructions else []
    lifter_.unimplemented.clear()
    lifter_.unimplemented.update(saved)
    if stmts[:len(prefix)] != prefix:
        raise RuntimeError("lift_flag_join: can't find the jump of block 0x%X" % bb.start)
    # A fused cmp/test + jcc emits the snapshot, then one statement for the jump.
    return len(stmts) - 1 if fused else len(prefix)


def lift_basic_block(lifter_, bb, flag_state=None):
    func = lifter_.func_start
    current[0] = (func, bb.start)
    joins_now[current[0]] = current[1]
    current[1] = None
    reads_now[current[0]] = set()
    stmts, out = _lift_block(lifter_, bb, flag_state=flag_state)
    outs_now[current[0]] = out
    if bb.start in need.get(func, ()) and bb.instructions:
        # A MAT state already sits in _fj. No state: a join assumed one the block doesn't end with
        # (translate_function refuses that join and lifts again).
        if not out or not out[0] or out[0].startswith(MAT):
            return stmts, out
        use = wanted.get(func, ())
        bits = {b: e for b, e in state_bits(out).items() if b in use}
        if not bits:
            return stmts, out
        expr = " | ".join("(uint32_t)!!(%s) << %d" % (bits[b], b) for b in sorted(bits))
        at = _jump_index(lifter_, bb, flag_state, stmts)
        stmts = stmts[:at] + ["_fj = %s; /* joined flags, %s */" % (expr, out[0])] + stmts[at:]
    return stmts, out


translator_self = [None]


def translate_function(self, func_addr, func_info):
    translator_self[0] = self
    for _ in range(8):
        saved = copy.deepcopy(self.lifter.unimplemented)
        joins_now.clear()
        reads_now.clear()
        outs_now.clear()
        code = _translate(self, func_addr, func_info)
        func = func_addr
        # The translator's back-edge probe runs at most three rounds, so a join can be made from
        # predecessor states that differ from what those blocks end with once emitted.
        joins = [j for j in joins_now.values() if j]
        wrong = {frozenset(j[1]) for j in joins
                 if any(outs_now.get((func, p)) != st for p, st in j[1].items())}
        # The joins whose state a consumer reads, directly or through a later join.
        by_origin = {j[2]: j for j in joins}
        reads = set().union(*reads_now.values())
        live, todo = set(), [o for _, o in reads]
        while todo:
            o = todo.pop()
            if o not in live and o in by_origin:
                live.add(o)
                todo.extend(by_origin[o][3])
        now = (set().union(*(by_origin[o][0] for o in live)), {b for b, _ in reads})
        if not wrong and now == (need.get(func, set()), wanted.get(func, set())):
            break
        need[func], wanted[func] = now
        refused.setdefault(func, set()).update(wrong)
        self.lifter.unimplemented.clear()
        self.lifter.unimplemented.update(saved)
    else:
        raise RuntimeError("lift_flag_join: 0x%X did not settle" % func_addr)
    if code and "_fj" in code:
        mixed[0] += code.count("/* joined flags")
        code = code.replace("    int _flags = 0; /* fallback flag var */",
                            "    int _flags = 0; /* fallback flag var */\n"
                            "    uint32_t _fj = 0; /* flags joined across setters (lift_flag_join.py) */", 1)
    return code


translator._merge_flag_states = merge_flag_states
translator._incoming_flag_state = incoming_flag_state
translator.lift_basic_block = lift_basic_block
translator.FunctionTranslator.translate_function = translate_function
lifter._make_condition = make_condition

if __name__ == "__main__":
    sys.argv[0] = "tools.recomp"
    try:
        main()
    finally:
        print("lift_flag_join: %d block join(s) merged across cmp/test widths, %d predecessor(s) "
              "write joined flags" % (joins[0], mixed[0]))
        print("lift_frndint: %d frndint site(s) round by the control word" % len(lift_frndint.sites))
