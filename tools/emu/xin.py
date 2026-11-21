#!/usr/bin/env python3
"""Scripted input for an emulator running under Xvfb, through XTest.

usage: xin.py "<cmd>;<cmd>;..."   (commands are listed in README.md)
"""
import ctypes
import os
import subprocess
import sys
import threading
import time

x11 = ctypes.CDLL('libX11.so.6')
xtst = ctypes.CDLL('libXtst.so.6')
x11.XInitThreads()
x11.XOpenDisplay.restype = ctypes.c_void_p
x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
x11.XStringToKeysym.restype = ctypes.c_ulong
x11.XStringToKeysym.argtypes = [ctypes.c_char_p]
x11.XFlush.argtypes = [ctypes.c_void_p]
xtst.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
xtst.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
xtst.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                      ctypes.c_ulong]
display = x11.XOpenDisplay(None)
x11.XDefaultRootWindow.restype = ctypes.c_ulong
x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
x11.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong),
                           ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
                           ctypes.POINTER(ctypes.c_uint)]
x11.XFetchName.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_char_p)]
x11.XMoveWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_int]
x11.XRaiseWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
x11.XSetInputFocus.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
x11.XFree.argtypes = [ctypes.c_void_p]


class XWindowAttributes(ctypes.Structure):
    _fields_ = [('x', ctypes.c_int), ('y', ctypes.c_int), ('width', ctypes.c_int), ('height', ctypes.c_int),
                ('border_width', ctypes.c_int), ('depth', ctypes.c_int), ('visual', ctypes.c_void_p),
                ('root', ctypes.c_ulong), ('c_class', ctypes.c_int), ('bit_gravity', ctypes.c_int),
                ('win_gravity', ctypes.c_int), ('backing_store', ctypes.c_int),
                ('backing_planes', ctypes.c_ulong), ('backing_pixel', ctypes.c_ulong),
                ('save_under', ctypes.c_int), ('colormaps', ctypes.c_ulong), ('map_installed', ctypes.c_int),
                ('map_state', ctypes.c_int), ('all_event_masks', ctypes.c_long),
                ('your_event_mask', ctypes.c_long), ('do_not_propagate_mask', ctypes.c_long),
                ('override_redirect', ctypes.c_int), ('screen', ctypes.c_void_p)]


x11.XGetWindowAttributes.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(XWindowAttributes)]


def emulator_windows():
    """The emulator's own windows (one per console), oldest first: the
    top-level windows that are shown and are not menus."""
    root = x11.XDefaultRootWindow(display)
    r, p = ctypes.c_ulong(), ctypes.c_ulong()
    children = ctypes.POINTER(ctypes.c_ulong)()
    count = ctypes.c_uint()
    x11.XQueryTree(display, root, ctypes.byref(r), ctypes.byref(p), ctypes.byref(children), ctypes.byref(count))
    found = []
    for i in range(count.value):
        w = children[i]
        attrs = XWindowAttributes()
        x11.XGetWindowAttributes(display, w, ctypes.byref(attrs))
        if attrs.map_state != 2 or attrs.override_redirect or attrs.width < 200 or attrs.height < 300:
            continue
        found.append((w, attrs.x, attrs.y, attrs.width, attrs.height))
    if children:
        x11.XFree(children)
    return sorted(found)


def move(x, y):
    xtst.XTestFakeMotionEvent(display, -1, x, y, 0)
    x11.XFlush(display)


def button(down):
    xtst.XTestFakeButtonEvent(display, 1, 1 if down else 0, 0)
    x11.XFlush(display)


def key(name, down):
    code = x11.XKeysymToKeycode(display, x11.XStringToKeysym(name.encode()))
    xtst.XTestFakeKeyEvent(display, code, 1 if down else 0, 0)
    x11.XFlush(display)


def shot(path):
    subprocess.run(['import', '-window', 'root', path], check=False)


def sweep(seconds, step):
    """Stylus down, swept left and right at a steady rate, for `seconds`."""
    move(30, 340)
    time.sleep(0.1)
    button(True)
    time.sleep(0.2)
    x, direction, end = 30.0, 1, time.time() + seconds
    while time.time() < end:
        x += direction * step
        if x > 240 or x < 30:
            direction = -direction
        move(int(x), 340)
        time.sleep(0.008)
    button(False)


for command in sys.argv[1].split(';'):
    a = command.split()
    if not a:
        continue
    if a[0] == 'sleep':
        time.sleep(float(a[1]))
    elif a[0] == 'move':
        move(int(a[1]), int(a[2]))
    elif a[0] == 'key':
        key(a[1], True)
        time.sleep(float(a[2]))
        key(a[1], False)
    elif a[0] == 'windows':
        # windows: where the emulator's windows are (to the terminal)
        for n, (w, x, y, width, height) in enumerate(emulator_windows()):
            print('window %d: %dx%d at %d,%d' % (n, width, height, x, y), flush=True)
    elif a[0] == 'place':
        # place <n> <x> <y>: put the emulator's n-th window (0: the first)
        # there. (Under Xvfb nothing arranges them: a second opens on top of
        # the first.)
        wins = emulator_windows()
        n = int(a[1])
        if n < len(wins):
            x11.XMoveWindow(display, wins[n][0], int(a[2]), int(a[3]))
            x11.XFlush(display)
    elif a[0] == 'focus':
        # focus <n>: the keys go to the emulator's n-th window
        wins = emulator_windows()
        n = int(a[1])
        if n < len(wins):
            x11.XRaiseWindow(display, wins[n][0])
            x11.XSetInputFocus(display, wins[n][0], 2, 0)
            x11.XFlush(display)
    elif a[0] == 'hold':
        # hold <key> / release <key>: a key held across other commands (two
        # buttons at once: "hold z;key Down 0.1;release z").
        key(a[1], True)
    elif a[0] == 'release':
        key(a[1], False)
    elif a[0] == 'ready':
        # ready [seconds]: wait until the game is on the screens -- in play,
        # or a menu (the title) -- (the emulator may take a long time over
        # the card before it starts, and how long depends on what has
        # changed on it), then a second more.
        import tempfile
        from PIL import Image
        probe = os.path.join(tempfile.gettempdir(), 'xin_ready_%d.png' % os.getpid())
        end = time.time() + (float(a[1]) if len(a) > 1 else 90)
        while time.time() < end:
            shot(probe)
            try:
                # (A picture, not the blank white the emulator starts with;
                # and the game's own pad drawn under it.)
                whole = Image.open(probe).convert('L')
                top = whole.crop((0, 120, 256, 247)).histogram()
                pad = whole.crop((0, 400, 256, 440)).histogram()
                # (Or, with a debug start, the readout written at the top
                # of the touch screen.)
                text = whole.crop((0, 247, 256, 300)).histogram()
                if sum(top[40:]) > 2000 and sum(top[230:]) < 16000 and (sum(pad[200:]) > 300 or sum(text[200:]) > 300):
                    break
                # (Or a menu -- the title, the pause: its orange words on the
                # touch screen. The emulator's own splash has some orange in
                # its logo, about 500 pixels; a menu, 1,600.)
                menu = Image.open(probe).convert('RGB').crop((0, 247, 256, 450))
                if sum(1 for r, g, b in menu.getdata() if r > 180 and 90 < g < 190 and b < 90) > 900:
                    break
            except OSError:
                pass
            time.sleep(0.5)
        if os.path.exists(probe):
            os.remove(probe)
        time.sleep(1.0)
    elif a[0] == 'tap':
        # tap <x> <y>: the stylus down and up again, quickly
        move(int(a[1]), int(a[2]))
        time.sleep(0.03)
        button(True)
        time.sleep(0.06)
        button(False)
    elif a[0] == 'down':
        # down <x> <y> ... up: the stylus put down and held, then lifted
        move(int(a[1]), int(a[2]))
        time.sleep(0.03)
        button(True)
    elif a[0] == 'up':
        button(False)
    elif a[0] == 'drag':
        x0, y0, x1, y1 = map(int, a[1:5])
        move(x0, y0)
        time.sleep(0.1)
        button(True)
        time.sleep(0.15)
        for i in range(1, 21):
            move(x0 + (x1 - x0) * i // 20, y0 + (y1 - y0) * i // 20)
            time.sleep(0.03)
        time.sleep(0.1)
        button(False)
    elif a[0] == 'sweep':
        sweep(float(a[1]), float(a[2]))
    elif a[0] == 'shot':
        shot(a[1])
    elif a[0] == 'burst':
        os.makedirs(a[1], exist_ok=True)
        for i in range(int(a[2])):
            shot('%s/t%02d.png' % (a[1], i))
    elif a[0] == 'sweepburst':
        # sweepburst <dir> <n> <px per step>: screenshots WHILE the view turns
        os.makedirs(a[1], exist_ok=True)
        worker = threading.Thread(target=sweep, args=(600.0, float(a[3])), daemon=True)
        worker.start()
        time.sleep(0.5)
        for i in range(int(a[2])):
            shot('%s/t%02d.png' % (a[1], i))
    else:
        sys.exit('unknown command: ' + command)
