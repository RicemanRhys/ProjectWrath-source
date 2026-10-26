"""Step 110 (notes 18.113): the saves-outside tests, with SYNTHETIC saves in scratch folders only.

  python t110.py SCRATCH TEST [TEST ...]      (or "all")

SCRATCH holds s\\N\\ (the build to test: crash_twoc.exe + xemu_apu.dll). Each test makes s\\t\\<test>\\ with a
package-like folder (mkpkg.ps1: ProjectWrath.exe or crash_twoc.exe, the DLL, game_files with default.xbe and Crashdat
as a junction to the repo's) and its own data folder, passed as RECOMP_DATA_DIR: nothing here uses the user's
%LOCALAPPDATA%\\ProjectWrath. Results: s\\t\\results.txt (one line per check: PASS/FAIL), the runs' logs in
s\\t\\<test>\\runs\\.
"""
import hashlib, os, random, re, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
SCR = S = N = TROOT = RESULTS = None   # set by setup() when run as the suite (t110b.sh imports synth() alone)
NA = "Dåta é Ω"           # a folder name with a space and letters outside ASCII and Windows-1252


def setup(scr):
    global SCR, S, N, TROOT, RESULTS
    SCR = os.path.abspath(scr)
    S = os.path.join(SCR, "s")
    N = os.path.join(S, "N")
    TROOT = os.path.join(S, "t")
    RESULTS = os.path.join(TROOT, "results.txt")
    os.makedirs(TROOT, exist_ok=True)
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def log(s):
    print(s, flush=True)
    with open(RESULTS, "a", encoding="utf-8") as f:
        f.write(s + "\n")


def check(test, ok, what):
    log("%s %-10s %s" % ("PASS" if ok else "FAIL", test, what))
    return ok


def ps(script, *args):
    r = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", os.path.join(HERE, script)]
                       + list(args), capture_output=True, text=True)
    if r.returncode:
        raise SystemExit("%s failed: %s %s" % (script, r.stdout, r.stderr))


def fresh(test):
    d = os.path.join(TROOT, test)
    if os.path.exists(d):
        ps("rmpkg.ps1", "-Dir", d)
    os.makedirs(os.path.join(d, "runs"))
    return d


def mkpkg(d, name="ProjectWrath.exe", src=None):
    ps("mkpkg.ps1", "-Dir", d, "-From", src or N, "-Name", name)
    return d


def tree(root):
    """relpath -> sha256 of every file under root (links not followed)."""
    out = {}
    if not os.path.isdir(root):
        return out
    for dp, dn, fn in os.walk(root):
        dn[:] = [x for x in dn if not os.path.islink(os.path.join(dp, x)) and
                 not (os.stat(os.path.join(dp, x), follow_symlinks=False).st_file_attributes & 0x400)]
        for f in fn:
            p = os.path.join(dp, f)
            out[os.path.relpath(p, root)] = hashlib.sha256(open(p, "rb").read()).hexdigest()
    return out


def sub(t, prefix):
    return {k: v for k, v in t.items() if k.split(os.sep)[0] in prefix}


def synth(gf, seed, backups=2):
    """A synthetic save in gf (a game_files or a data folder's saves): UDATA\\56550003 with the title's three .xbx and
    the save 4D5C926D6357 (crash.sav, SaveMeta.xbx), TDATA\\56550003 (empty), UDATA_backups with `backups` backups,
    the newest equal to the save (so a start makes no new one)."""
    r = random.Random(seed)
    u = os.path.join(gf, "UDATA", "56550003")
    sv = os.path.join(u, "4D5C926D6357")
    os.makedirs(sv, exist_ok=True)
    os.makedirs(os.path.join(gf, "TDATA", "56550003"), exist_ok=True)
    meta = ("﻿Name=SYNTH%d\r\n" % seed).encode("utf-16-le")
    for n in ("TitleMeta.xbx", "TitleImage.xbx", "SaveImage.xbx"):
        open(os.path.join(u, n), "wb").write(("﻿TitleName=synthetic %s\r\n" % n).encode("utf-16-le"))
    data = bytes(r.getrandbits(8) for _ in range(12784))
    open(os.path.join(sv, "crash.sav"), "wb").write(data)
    open(os.path.join(sv, "SaveMeta.xbx"), "wb").write(meta)
    for i in range(backups):
        b = os.path.join(gf, "UDATA_backups", "2026-10-%02d_10-00-00" % (10 + i))
        os.makedirs(b, exist_ok=True)
        bd = data if i == backups - 1 else bytes(r.getrandbits(8) for _ in range(12784))
        open(os.path.join(b, "crash.sav"), "wb").write(bd)
        open(os.path.join(b, "SaveMeta.xbx"), "wb").write(meta)
        open(os.path.join(b, "backup.txt"), "w").write("Project Wrath save backup\r\nwhy: synthetic\r\n")
    return hashlib.sha256(data).hexdigest()


def run(test, d, tag, exe, root, secs=26, data=None, menu=True, extra=(), opts=()):
    a = [sys.executable, os.path.join(HERE, "run110.py"), tag, exe, root, str(secs), "--out=" + os.path.join(d, "runs")]
    if menu:
        a.append("--menu")
    a += list(opts)
    if data:
        a.append("RECOMP_DATA_DIR=" + data)
    a += list(extra)
    r = subprocess.run(a, capture_output=True, text=True, encoding="utf-8", errors="replace")
    print("   ", r.stdout.strip(), r.stderr.strip()[-300:])
    p = os.path.join(d, "runs", tag + ".err.log")
    return open(p, "rb").read().decode("utf-8", "replace") if os.path.exists(p) else ""


def startmsg(lg):
    m = re.search(r'host startup message: "([^"]*)"', lg)
    return m.group(1) if m else None


# ── the tests ────────────────────────────────────────────────────────────

def t_new(t):
    d = fresh(t)
    pk = mkpkg(os.path.join(d, "pk a", "ProjectWrath-Share"))
    data = os.path.join(d, "data")
    lg = run(t, d, "r1", os.path.join(pk, "ProjectWrath.exe"), pk, data=data)
    check(t, "[SAVES] outside the game's folder" in lg, "outside layout chosen")
    check(t, os.path.isfile(os.path.join(data, "saves", "UDATA", "56550003", "TitleMeta.xbx")),
          "the title's files written in the data folder")
    check(t, not os.path.exists(os.path.join(pk, "game_files", "UDATA")) and
          not os.path.exists(os.path.join(pk, "game_files", "TDATA")), "no UDATA/TDATA in the package")
    check(t, os.path.isfile(os.path.join(data, "cache", "Partition0.img")) and
          os.path.isdir(os.path.join(data, "cache", "Cache")), "the toolkit's disk and cache in data\\cache")
    check(t, os.path.isfile(os.path.join(data, "About this folder.txt")), "About this folder.txt")
    check(t, startmsg(lg) == "Press F1 to open the settings menu", "no saves message on a new install")
    check(t, "MAIN MENU OPEN" in lg, "main menu")


def t_newself(t):
    d = fresh(t)
    pk = mkpkg(os.path.join(d, "pk a", "ProjectWrath-Share"))
    data = os.path.join(d, "data")
    lg = run(t, d, "r1", os.path.join(pk, "ProjectWrath.exe"), pk, data=data, opts=["--self"])
    check(t, "[START] started by itself" in lg, "started by itself (RECOMP_DATA_DIR doesn't make a launcher)")
    check(t, os.path.isfile(os.path.join(data, "ProjectWrath.ini")) and
          not os.path.exists(os.path.join(pk, "ProjectWrath.ini")), "first-run ini in the data folder, none beside the exe")
    check(t, os.path.isfile(os.path.join(pk, "ProjectWrath.err.log")), "logs beside the exe as before")
    check(t, "MAIN MENU OPEN" in lg, "main menu")


def mig_fixture(d, seed=1, name="ProjectWrath.exe"):
    pk = mkpkg(os.path.join(d, "pk a", "ProjectWrath-Share"), name)
    h = synth(os.path.join(pk, "game_files"), seed)
    return pk, h


def t_mig(t):
    d = fresh(t)
    pk, h = mig_fixture(d)
    data = os.path.join(d, "data")
    before = tree(os.path.join(pk, "game_files"))
    lg = run(t, d, "r1", os.path.join(pk, "ProjectWrath.exe"), pk, data=data)
    after = tree(os.path.join(pk, "game_files"))
    dt = tree(os.path.join(data, "saves"))
    src = sub(before, ("UDATA", "UDATA_backups", "TDATA"))
    check(t, before == after, "the package's game_files unchanged (%d files)" % len(before))
    check(t, all(dt.get(k) == v for k, v in src.items()), "every save file copied, SHA-256 equal (%d)" % len(src))
    check(t, set(sub(dt, ("UDATA", "UDATA_backups"))) == set(sub(src, ("UDATA", "UDATA_backups"))),
          "nothing else in the data folder's UDATA / backups")
    check(t, os.path.isdir(os.path.join(data, "saves", "TDATA", "56550003")), "TDATA copied")
    check(t, "[SAVES] the saves copied from" in lg and "SHA-256 checked" in lg, "logged")
    m = startmsg(lg) or ""
    check(t, m.startswith("Your saves were copied to ") and m.endswith("The old copy was left where it was."),
          "the one-time message: %r" % m[:90])
    check(t, not os.path.exists(os.path.join(data, "saves.migrating")) and
          not os.path.exists(os.path.join(data, "saves", "migration in progress.txt")), "no staging or journal left")
    check(t, "at start: the save equals the backup" in lg, "the start's backup sees the copied backups")
    # the second start: nothing copied again, no message
    lg = run(t, d, "r2", os.path.join(pk, "ProjectWrath.exe"), pk, data=data)
    check(t, "[SAVES] the saves copied" not in lg and startmsg(lg) == "Press F1 to open the settings menu",
          "second start: no copy, no message")
    check(t, tree(os.path.join(data, "saves")) == dt or
          all(tree(os.path.join(data, "saves")).get(k) == v for k, v in dt.items() if "UDATA" in k),
          "second start: the data folder's saves unchanged")
    check(t, tree(os.path.join(pk, "game_files")) == before, "second start: the package unchanged")


def t_both(t):
    d = fresh(t)
    pk, hp = mig_fixture(d, seed=2)
    data = os.path.join(d, "data")
    hd = synth(os.path.join(data, "saves"), seed=1)
    pkb = tree(os.path.join(pk, "game_files"))
    lg = run(t, d, "r1", os.path.join(pk, "ProjectWrath.exe"), pk, data=data)
    sav = os.path.join(data, "saves", "UDATA", "56550003", "4D5C926D6357", "crash.sav")
    check(t, hashlib.sha256(open(sav, "rb").read()).hexdigest() == hd, "the data folder's save used and unchanged")
    check(t, tree(os.path.join(pk, "game_files")) == pkb, "the package's save unchanged")
    check(t, "saves in both places" in lg, "logged")
    m = startmsg(lg) or ""
    check(t, m.startswith("Saves were found in two places."), "message: %r" % m[:80])
    check(t, os.path.isfile(os.path.join(data, "Old save copies.txt")), "Old save copies.txt lists the old copy")
    lg = run(t, d, "r2", os.path.join(pk, "ProjectWrath.exe"), pk, data=data)
    check(t, startmsg(lg) == "Press F1 to open the settings menu" and "saves in both places" in lg,
          "second start: logged, no message again")


def t_replaced(t):
    d = fresh(t)
    pk, h = mig_fixture(d)
    data = os.path.join(d, "data")
    run(t, d, "r1", os.path.join(pk, "ProjectWrath.exe"), pk, data=data)
    dt = tree(os.path.join(data, "saves"))
    pk2 = mkpkg(os.path.join(d, "pk b (new version)", "ProjectWrath-Share"))
    lg = run(t, d, "r2", os.path.join(pk2, "ProjectWrath.exe"), pk2, data=data)
    check(t, "at start: the save equals the backup" in lg, "the new package's start found the save")
    check(t, not os.path.exists(os.path.join(pk2, "game_files", "UDATA")), "nothing made in the new package's game_files")
    sav = os.path.join("UDATA", "56550003", "4D5C926D6357", "crash.sav")
    check(t, tree(os.path.join(data, "saves")).get(sav) == h, "the save survived the package's replacement")
    # the old package folder deleted: the saves still there
    ps("rmpkg.ps1", "-Dir", os.path.dirname(pk))
    lg = run(t, d, "r3", os.path.join(pk2, "ProjectWrath.exe"), pk2, data=data)
    check(t, tree(os.path.join(data, "saves")).get(sav) == h and "at start: the save equals the backup" in lg,
          "the old package folder deleted: the save is still used")


def t_portable(t):
    d = fresh(t)
    pk, h = mig_fixture(d, seed=2)
    open(os.path.join(pk, "portable.txt"), "w").write("keep everything in this folder\r\n")
    data = os.path.join(d, "data")
    lg = run(t, d, "r1", os.path.join(pk, "ProjectWrath.exe"), pk, data=data)
    check(t, "portable.txt is next to the exe" in lg, "portable mode logged")
    check(t, not os.path.exists(data), "the data folder not made")
    check(t, "at start: the save equals the backup" in lg, "the package's save used")


def t_inpkg(t):
    d = fresh(t)
    pk, h = mig_fixture(d, seed=2)
    data = os.path.join(d, "data")
    lg = run(t, d, "r1", os.path.join(pk, "ProjectWrath.exe"), pk, data=data, extra=["RECOMP_SAVES_IN_PACKAGE=1"])
    check(t, "RECOMP_SAVES_IN_PACKAGE=1" in lg and not os.path.exists(data), "forced into the package, no data folder")
    check(t, "at start: the save equals the backup" in lg, "the package's save used")


def t_dev(t):
    d = fresh(t)
    root = mkpkg(os.path.join(d, "dev root"), "crash_twoc.exe")
    data = os.path.join(d, "data")
    lg = run(t, d, "r1", os.path.join(root, "crash_twoc.exe"), root, data=data)
    check(t, "[SAVES] in the game's folder this run" in lg and not os.path.exists(data),
          "the developer's exe keeps the package layout, no data folder")
    check(t, os.path.isfile(os.path.join(root, "game_files", "UDATA", "56550003", "TitleMeta.xbx")),
          "its UDATA in game_files as before")
    lg = run(t, d, "r2", os.path.join(root, "crash_twoc.exe"), root, data=data, extra=["RECOMP_SAVES_OUTSIDE=1"])
    check(t, "[SAVES] outside the game's folder" in lg, "RECOMP_SAVES_OUTSIDE=1 moves it")
    check(t, "the saves copied from" in lg and os.path.isfile(os.path.join(data, "saves", "UDATA", "56550003", "TitleMeta.xbx")),
          "... and copies its UDATA in")


def t_ro(t):
    d = fresh(t)
    pk, h = mig_fixture(d, seed=2)
    data = os.path.join(d, "data")
    os.makedirs(data)
    user = os.environ["USERNAME"]
    subprocess.run(["icacls", data, "/deny", "%s:(OI)(CI)(WD,AD)" % user], capture_output=True)
    try:
        lg = run(t, d, "r1", os.path.join(pk, "ProjectWrath.exe"), pk, data=data)
    finally:
        subprocess.run(["icacls", data, "/remove:d", user], capture_output=True)
    check(t, "[SAVES] in the game's folder this run" in lg, "fell back to the package's layout")
    m = startmsg(lg) or ""
    check(t, m.startswith("Your saves stay in the game's folder"), "said why: %r" % m[:110])
    check(t, "at start: the save equals the backup" in lg, "the package's save used")
    check(t, not os.listdir(data), "the read-only data folder untouched")


def t_full(t):
    d = fresh(t)
    pk, h = mig_fixture(d, seed=2)
    data = os.path.join(d, "data")
    lg = run(t, d, "r1", os.path.join(pk, "ProjectWrath.exe"), pk, data=data, extra=["RECOMP_DEV_SAVES_FAIL=write"])
    m = startmsg(lg) or ""
    check(t, "[SAVES] in the game's folder this run" in lg and "the disk is full" in m, "full disk: fallback, said why")
    check(t, not os.path.exists(os.path.join(data, "saves", "UDATA")), "nothing copied")


def t_copyfail(t):
    d = fresh(t)
    pk, h = mig_fixture(d)
    data = os.path.join(d, "data")
    before = tree(os.path.join(pk, "game_files"))
    lg = run(t, d, "r1", os.path.join(pk, "ProjectWrath.exe"), pk, data=data, extra=["RECOMP_DEV_SAVES_FAIL=copy@3"])
    m = startmsg(lg) or ""
    check(t, "[SAVES] in the game's folder this run" in lg and m.startswith("Your saves couldn't be copied"),
          "copy failure: fallback, said why: %r" % m[:100])
    check(t, not os.path.exists(os.path.join(data, "saves.migrating")) and
          not os.path.exists(os.path.join(data, "saves", "UDATA")), "staging deleted, nothing left in the data folder")
    check(t, tree(os.path.join(pk, "game_files")) == before, "the package unchanged")
    lg = run(t, d, "r2", os.path.join(pk, "ProjectWrath.exe"), pk, data=data, extra=["RECOMP_DEV_SAVES_FAIL=hash@2"])
    check(t, "a copy read back different" in lg and not os.path.exists(os.path.join(data, "saves", "UDATA")),
          "a copy that reads back different: refused, nothing left")
    lg = run(t, d, "r3", os.path.join(pk, "ProjectWrath.exe"), pk, data=data)
    check(t, "the saves copied from" in lg, "the next start copies them")


def t_movefail(t):
    d = fresh(t)
    pk, h = mig_fixture(d)
    data = os.path.join(d, "data")
    lg = run(t, d, "r1", os.path.join(pk, "ProjectWrath.exe"), pk, data=data, extra=["RECOMP_DEV_SAVES_FAIL=move@2"])
    check(t, "moves into" in lg and "taken back" in lg, "a refused move: the moves taken back")
    left = tree(os.path.join(data, "saves"))
    check(t, not sub(left, ("UDATA", "UDATA_backups", "TDATA")) and not os.path.exists(os.path.join(data, "saves.migrating"))
          and not os.path.exists(os.path.join(data, "saves", "migration in progress.txt")),
          "the data folder as it was (no save, backups, staging or journal)")
    check(t, "[SAVES] in the game's folder this run" in lg, "fallback this run")
    lg = run(t, d, "r2", os.path.join(pk, "ProjectWrath.exe"), pk, data=data)
    check(t, "the saves copied from" in lg, "the next start copies them")


def t_killcopy(t):
    d = fresh(t)
    pk, h = mig_fixture(d)
    data = os.path.join(d, "data")
    run(t, d, "r1", os.path.join(pk, "ProjectWrath.exe"), pk, data=data,
        extra=["RECOMP_DEV_SAVES_HOLD=copy@3:8000"], opts=["--kill=test hold.*copy@3"])
    check(t, os.path.isdir(os.path.join(data, "saves.migrating")) and
          not os.path.exists(os.path.join(data, "saves", "UDATA")), "killed mid-copy: staging left, no save placed")
    before = tree(os.path.join(pk, "game_files"))
    lg = run(t, d, "r2", os.path.join(pk, "ProjectWrath.exe"), pk, data=data)
    check(t, "stopped part-way" in lg and "the saves copied from" in lg, "next start: staging deleted, copied again")
    dt = tree(os.path.join(data, "saves"))
    check(t, all(dt.get(k) == v for k, v in sub(before, ("UDATA", "UDATA_backups", "TDATA")).items()), "complete")


def t_killcommit(t):
    d = fresh(t)
    pk, h = mig_fixture(d)
    data = os.path.join(d, "data")
    run(t, d, "r1", os.path.join(pk, "ProjectWrath.exe"), pk, data=data,
        extra=["RECOMP_DEV_SAVES_HOLD=commit@1:8000"], opts=["--kill=test hold.*commit@1"])
    check(t, os.path.isfile(os.path.join(data, "saves", "migration in progress.txt")), "killed mid-commit: journal left")
    before = tree(os.path.join(pk, "game_files"))
    lg = run(t, d, "r2", os.path.join(pk, "ProjectWrath.exe"), pk, data=data)
    check(t, "stopped last time was finished" in lg, "next start finished the commit")
    dt = tree(os.path.join(data, "saves"))
    check(t, all(dt.get(k) == v for k, v in sub(before, ("UDATA", "UDATA_backups", "TDATA")).items()) and
          not os.path.exists(os.path.join(data, "saves.migrating")), "complete, staging gone")
    check(t, (startmsg(lg) or "").startswith("Your saves were copied"), "the message shown at that start")


def t_nonascii(t):
    d = fresh(t)
    pk, h = mig_fixture(d)
    data = os.path.join(d, NA)    # 175 characters here: under the 190 limit (the scratch folder is long)
    lg = run(t, d, "r1", os.path.join(pk, "ProjectWrath.exe"), pk, data=data)
    sav = os.path.join(data, "saves", "UDATA", "56550003", "4D5C926D6357", "crash.sav")
    check(t, os.path.isfile(sav) and hashlib.sha256(open(sav, "rb").read()).hexdigest() == h, "copied into a non-ASCII path")
    check(t, os.path.isfile(os.path.join(data, "saves", "UDATA", "56550003", "TitleMeta.xbx")) and "MAIN MENU OPEN" in lg,
          "the game's opens there worked, main menu")
    check(t, "at start: the save equals the backup" in lg, "backups found there")


def t_restore(t):
    d = fresh(t)
    pk, h = mig_fixture(d)
    data = os.path.join(d, "data")
    run(t, d, "r1", os.path.join(pk, "ProjectWrath.exe"), pk, data=data)
    bk = os.path.join(data, "saves", "UDATA_backups")
    old = sorted(x for x in os.listdir(bk) if x.startswith("2026-10-1"))[0]
    want = hashlib.sha256(open(os.path.join(bk, old, "crash.sav"), "rb").read()).hexdigest()
    open(os.path.join(bk, "restore at next start.txt"), "w").write(old + "\r\n")
    lg = run(t, d, "r2", os.path.join(pk, "ProjectWrath.exe"), pk, data=data)
    sav = os.path.join(data, "saves", "UDATA", "56550003", "4D5C926D6357", "crash.sav")
    check(t, "restored the backup %s" % old in lg and hashlib.sha256(open(sav, "rb").read()).hexdigest() == want,
          "restore at next start into the data folder")
    check(t, not os.path.exists(os.path.join(pk, "game_files", "UDATA_backups", "restore at next start.txt")),
          "the package's copy untouched")


def wait_log(path_fn, pat, secs):
    t0 = time.time()
    while time.time() - t0 < secs:
        p = path_fn()
        if p and os.path.exists(p) and pat.encode() in open(p, "rb").read():
            return p
        time.sleep(1)
    return None


def close_window(title="Project Wrath"):
    import ctypes
    u = ctypes.windll.user32
    h = u.FindWindowW(None, title)
    if h:
        u.PostMessageW(h, 0x10, 0, 0)
    return bool(h)


def kit_once(t, d, kit, data, portable):
    """The capture kit's replica: kit\\run.cmd (the repo's), game\\ProjectWrath.exe + DLL + game_files, captures\\;
    run like its numbered launchers (cmd, a "1 test.cmd" calling run.cmd), RECOMP_DATA_DIR in the environment."""
    import glob
    os.makedirs(os.path.join(kit, "kit"), exist_ok=True)
    os.makedirs(os.path.join(kit, "captures"), exist_ok=True)
    shutil.copy(os.path.join(HERE, "..", "..", "capturekit", "kit", "run.cmd"), os.path.join(kit, "kit", "run.cmd"))
    mkpkg(os.path.join(kit, "game"))
    if portable:
        open(os.path.join(kit, "game", "portable.txt"), "w").write("this copy keeps everything in its own folder\r\n")
    open(os.path.join(kit, "1 test.cmd"), "w").write('@echo off\r\ncall "%~dp0kit\\run.cmd" 1_test\r\n')
    open(os.path.join(d, "go.cmd"), "w").write('@echo off\r\ncall "%s"\r\n' % os.path.join(kit, "1 test.cmd"))
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(("RECOMP_", "TWOC_"))}
    env["RECOMP_DATA_DIR"] = data
    p = subprocess.Popen(["cmd", "/c", os.path.join(d, "go.cmd")], cwd="C:\\", env=env, stdin=subprocess.DEVNULL,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def logp():
        c = glob.glob(os.path.join(kit, "captures", "1_test_*", "run.err.log"))
        return c[0] if c else None
    lp = wait_log(logp, "MAIN MENU OPEN", 120)
    time.sleep(2)
    close_window()
    try:
        p.communicate(timeout=60)
    except subprocess.TimeoutExpired:
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(p.pid)], capture_output=True)
    return open(lp, "rb").read().decode("utf-8", "replace") if lp else ""


def t_kit(t):
    d = fresh(t)
    kit = os.path.join(d, "Capture kit")
    data = os.path.join(d, "data (must stay absent)")
    lg = kit_once(t, d, kit, data, True)
    check(t, "portable.txt is next to the exe" in lg and "MAIN MENU OPEN" in lg, "the kit's exe with portable.txt: the package layout")
    check(t, not os.path.exists(data), "no data folder made (the user's would be untouched)")
    check(t, os.path.isfile(os.path.join(kit, "game", "game_files", "UDATA", "56550003", "TitleMeta.xbx")),
          "its saves stay in the kit's own game_files")
    d2 = os.path.join(d, "control")
    os.makedirs(d2)
    kit2 = os.path.join(d2, "Capture kit")
    lg = kit_once(t, d2, kit2, data, False)
    check(t, "[SAVES] outside the game's folder" in lg and os.path.isdir(data),
          "control: without portable.txt the kit's exe would use the data folder")


def t_dxvk(t):
    d = fresh(t)
    rep = os.path.join(d, "DXVK-test")
    os.makedirs(rep)
    shutil.copy(os.path.join(HERE, "..", "step109", "dxvk", "Play Project Wrath (DXVK).bat"), rep)
    for f in ("crash_twoc.exe", "xemu_apu.dll"):
        shutil.copy(os.path.join(N, f), os.path.join(rep, "ProjectWrath.exe" if f.endswith(".exe") else f))
    open(os.path.join(rep, "portable.txt"), "w").write("this copy keeps everything in its own folder\r\n")
    lgo = mkpkg(os.path.join(d, "ProjectWrath-LegionGo"))
    data = os.path.join(d, "data (must stay absent)")
    open(os.path.join(d, "go.cmd"), "w").write('@echo off\r\ncall "%s"\r\n' %
                                               os.path.join(rep, "Play Project Wrath (DXVK).bat"))
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(("RECOMP_", "TWOC_"))}
    env["RECOMP_DATA_DIR"] = data
    p = subprocess.Popen(["cmd", "/c", os.path.join(d, "go.cmd")], cwd="C:\\", env=env, stdin=subprocess.DEVNULL,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    lp = wait_log(lambda: os.path.join(rep, "run_dxvk.err.log"), "MAIN MENU OPEN", 120)
    time.sleep(2)
    close_window()
    try:
        p.communicate(timeout=60)
    except subprocess.TimeoutExpired:
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(p.pid)], capture_output=True)
    lg = open(lp, "rb").read().decode("utf-8", "replace") if lp else ""
    check(t, "portable.txt is next to the exe" in lg and "MAIN MENU OPEN" in lg, "the DXVK folder's exe with portable.txt: the package layout")
    check(t, not os.path.exists(data), "no data folder made")
    check(t, os.path.isfile(os.path.join(lgo, "game_files", "UDATA", "56550003", "TitleMeta.xbx")),
          "its saves in the Legion Go package's game_files, as before")


TESTS = [("new", t_new), ("newself", t_newself), ("mig", t_mig), ("both", t_both), ("replaced", t_replaced),
         ("portable", t_portable), ("inpkg", t_inpkg), ("dev", t_dev), ("ro", t_ro), ("full", t_full),
         ("copyfail", t_copyfail), ("movefail", t_movefail), ("killcopy", t_killcopy), ("killcommit", t_killcommit),
         ("nonascii", t_nonascii), ("restore", t_restore), ("kit", t_kit), ("dxvk", t_dxvk)]

if __name__ == "__main__":
    setup(sys.argv[1])
    want = sys.argv[2:]
    for name, fn in TESTS:
        if "all" in want or name in want:
            log("--- %s %s" % (name, time.strftime("%H:%M:%S")))
            try:
                fn(name)
            except Exception as e:      # a test's own error: recorded, the rest go on
                check(name, False, "test error: %r" % e)
