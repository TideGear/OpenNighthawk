"""Damaged-install routes: a route file may carry lines `--damage` / `ACTION:FILE` (a pair per damage).

    --damage
    poke=18:FF:LABSLOGO.SS
    --damage
    remove:PALETTES.PAL

ACTION is one of `remove`, `empty`, `trunc=N`, `poke=OFFSET:HEXBYTES`, `rename=NEW` (FILE is a name in the install's
top directory, matched without regard to case). `expand(args)` turns the pairs into one `--data DIR` argument (the
runner honours the last `--data`), where DIR is a private directory of hard links to the install (copies if linking
fails) with the named files damaged. DIR is named after a hash of the install's file list, sizes and the damage, and
is made once. The install itself is never written: the machine reads the data directory only, and never modifies it.

A player's install can be broken this way (a missing or damaged data file), so these routes run the original programs'
own error handling on a real route. Only damage a file-system could produce is allowed; no state is poked and no
executable is patched.
"""
import hashlib
import os
import shutil

MASTER = os.environ.get("F117_DATA", "D:/GOG/F-117A")
CACHE = os.environ.get("F117_DAMAGED_DATA",
                       os.path.join(os.path.expanduser("~"), "f117-recomp-local", "damaged-data"))


def set_master(path):
    """The install the tools were given (--data); damaged copies are made from it."""
    global MASTER
    MASTER = path


def parse(args):
    """(damage actions as [(action, FILE)], the remaining arguments)."""
    damage, rest, i = [], [], 0
    while i < len(args):
        if args[i] == "--damage":
            if i + 1 >= len(args):
                raise ValueError("--damage needs ACTION:FILE")
            action, _, name = args[i + 1].rpartition(":")
            if not action or not name:
                raise ValueError("bad damage %r (ACTION:FILE)" % args[i + 1])
            damage.append((action, name))
            i += 2
        else:
            rest.append(args[i])
            i += 1
    return damage, rest


def _apply(action, source, target):
    if action == "remove":
        return
    data = None
    if action == "empty":
        data = b""
    elif action.startswith("trunc="):
        with open(source, "rb") as f:
            data = f.read(int(action[6:], 0))
    elif action.startswith("poke="):
        offset, _, hexbytes = action[5:].partition(":")
        with open(source, "rb") as f:
            data = bytearray(f.read())
        patch = bytes.fromhex(hexbytes)
        start = int(offset, 0)
        if start + len(patch) > len(data):
            raise ValueError("poke past the end of %s" % os.path.basename(source))
        data[start:start + len(patch)] = patch
    else:
        raise ValueError("unknown damage action %r" % action)
    with open(target, "wb") as f:
        f.write(bytes(data))


def make(damage, master=None):
    """The damaged data directory for these actions (made once, named after its content)."""
    master = master or MASTER
    names = sorted(n for n in os.listdir(master) if os.path.isfile(os.path.join(master, n)))
    h = hashlib.sha256()
    for n in names:
        h.update(("%s:%d\n" % (n.upper(), os.path.getsize(os.path.join(master, n)))).encode())
    for action, name in sorted(damage):
        h.update(("%s=%s\n" % (action, name.upper())).encode())
    out = os.path.join(CACHE, h.hexdigest()[:24])
    if os.path.isfile(os.path.join(out, ".complete")):
        return out
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    todo = {name.upper(): action for action, name in damage}
    for n in names:
        source, target = os.path.join(master, n), os.path.join(out, n)
        action = todo.pop(n.upper(), None)
        if action is None:
            try:
                os.link(source, target)
            except OSError:
                shutil.copyfile(source, target)
        elif action.startswith("rename="):
            shutil.copyfile(source, os.path.join(out, action[7:]))
        else:
            _apply(action, source, target)
    if todo:
        raise ValueError("damage names files the install does not have: %s" % sorted(todo))
    open(os.path.join(out, ".complete"), "w").close()
    return out


def expand(args):
    """args without the --damage pairs, plus `--data DIR` for the damaged copy; unchanged when there is no damage."""
    damage, rest = parse(args)
    if not damage:
        return args
    return rest + ["--data", make(damage)]
