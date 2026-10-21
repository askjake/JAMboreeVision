#!/usr/bin/env python3
# JAMboreeVision -> virtual DART board integration test.
#
# Drives the REAL application code paths (jamboree.controller and the
# vision.app batch endpoint) against firmware/test/pty_sim, which runs the
# REAL dart_core.cpp logic behind a PTY.  No physical board, no live config:
#
#   * JAMBOREE_BASE and JAMBOREE_VISION_DATA are redirected to a fresh temp
#     directory BEFORE the application is imported, so the operator base.txt
#     is neither read nor written.
#   * The serial endpoint is a PTY created by pty_sim, never /dev/ttyACM0.
#
# Run:  python3 firmware/test/vision_virtual_serial_test.py
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
SIM = HERE / 'pty_sim'

PASS, FAIL = [], []


def check(name, ok, detail=''):
    (PASS if ok else FAIL).append(name)
    print(('  PASS  ' if ok else '  FAIL  ') + name + ((' :: ' + detail) if detail else ''))
    return ok


class Sim(object):
    # pty_sim lifecycle plus access to the firmware's real emissions.
    def __init__(self, tmp, tag, env_extra=None):
        self.logpath = Path(tmp) / ('sim_%s.log' % tag)
        env = dict(os.environ)
        for k in ('DART_SIM_NOREAD', 'DART_SIM_NOACK', 'DART_SIM_LATEACK'):
            env.pop(k, None)
        env.update(env_extra or {})
        self._log = open(self.logpath, 'w')
        self.proc = subprocess.Popen([str(SIM)], stdout=subprocess.PIPE,
                                     stderr=self._log, text=True, env=env)
        first = self.proc.stdout.readline().strip()
        m = re.match(r'^PTY (\S+)$', first)
        if not m:
            raise RuntimeError('pty_sim did not announce a PTY: %r' % first)
        self.pty = m.group(1)

    def text(self):
        self._log.flush()
        return self.logpath.read_text(errors='replace')

    def fwout(self):
        return [l[6:] for l in self.text().splitlines() if l.startswith('FWOUT ')]

    def hostlines(self):
        return [l.split(' ', 2)[2] for l in self.text().splitlines()
                if l.startswith('HOSTLINE ')]

    def events(self):
        # Parsed [EVT] outcome records emitted by the firmware itself.
        out = []
        for line in self.fwout():
            m = re.search(r'\[EVT\] r=(\d+) key=0x([0-9A-Fa-f]{2}) act=(\w+) result=(\w+)', line)
            if m:
                out.append({'r': int(m.group(1)), 'key': int(m.group(2), 16),
                            'act': m.group(3), 'result': m.group(4)})
        return out

    def stop(self):
        try:
            self.proc.terminate()
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()
        self._log.flush()
        self._log.close()


def write_base(path, pty, entries):
    doc = {'stbs': {}}
    for alias, remote in entries:
        doc['stbs'][alias] = {
            'alias': alias, 'stb': 'R000000000%s-00' % remote,
            'ip': '192.0.2.%s' % remote, 'protocol': 'RF',
            'remote': str(remote), 'com_port': pty,
            'role': 'hopper' if remote == '1' else 'joey',
            'host': 'Hopper-A',
        }
    Path(path).write_text(json.dumps(doc, indent=2))
    return doc


def wait_ready(serial_mgr, alias, timeout=20.0):
    end = time.time() + timeout
    while time.time() < end:
        if serial_mgr.has_port(alias, require_ready=True):
            return True
        time.sleep(0.25)
    return False


def main():
    tmp = tempfile.mkdtemp(prefix='dart-vision-virt-')
    base = Path(tmp) / 'base.txt'

    # ---- isolate application data BEFORE importing the application ----
    os.environ['JAMBOREE_BASE'] = str(base)
    os.environ['JAMBOREE_VISION_DATA'] = tmp
    os.environ['JAMBOREE_AUTOPAIR'] = '0'
    sys.path.insert(0, str(REPO))

    print('workspace     : %s' % tmp)
    print('live base.txt : NOT referenced (JAMBOREE_BASE redirected)')

    entries = [('Hopper-A', '1'), ('Joey-A', '2'), ('Joey-B', '3')]
    sim = Sim(tmp, 'main')
    doc = write_base(base, sim.pty, entries)
    print('virtual port  : %s' % sim.pty)

    from vision.app import create_app
    import jamboree.app as legacy
    from jamboree.serial_hub import serial_mgr

    app = create_app(tmp)
    legacy.store.reload()
    legacy.init_serial_from_base(doc)          # the application's own mapping code
    ctl = legacy.ctl

    try:
        check('virtual port opened by the application serial worker',
              wait_ready(serial_mgr, 'Hopper-A'),
              str(serial_mgr.status('Hopper-A').get('last_error') or ''))
        check('application resolved the PTY, not /dev/ttyACM0',
              serial_mgr.port_for('Hopper-A') == sim.pty and '/dev/ttyACM' not in sim.pty,
              str(serial_mgr.port_for('Hopper-A')))

        # S1 firmware identity: emitted at start-up, and retrievable on demand.
        # The application no longer toggles DTR, so it does NOT reset the board
        # on open and may legitimately miss a banner printed before it attached.
        # Both properties are therefore checked separately.
        time.sleep(0.5)
        check('firmware emits an identity banner at start-up',
              any('DART-BRIDGE' in l for l in sim.fwout()),
              str(sim.fwout()[:2]))
        f0 = len(sim.fwout())
        serial_mgr.write('Hopper-A', b'version\n', require_ready=True, wait=True)
        time.sleep(0.8)
        check('firmware identity is retrievable on demand after the host attaches',
              any('DART-BRIDGE' in l for l in sim.fwout()[f0:]),
              str(sim.fwout()[f0:]))

        # S2 tap
        n0 = len(sim.events())
        ctl.handle_auto_remote('1', 'Hopper-A', 'guide', 120, force='rf')
        time.sleep(1.2)
        tap = sim.events()[n0:]
        check('application tap produced a confirmed DOWN and UP on remote 1',
              len([e for e in tap if e['r'] == 1 and e['result'] == 'confirmed']) >= 2,
              str(tap))

        # S3 hold
        n0 = len(sim.events())
        t0 = time.time()
        ctl.handle_auto_remote('1', 'Joey-A', 'guide', 3000, force='rf')
        elapsed = time.time() - t0
        time.sleep(1.0)
        hold = sim.events()[n0:]
        check('application 3 s hold on remote 2 released only after the hold',
              elapsed >= 3.0 and
              len([e for e in hold if e['r'] == 2 and e['act'] == 'up'
                   and e['result'] == 'confirmed']) >= 1,
              'elapsed=%.2fs %s' % (elapsed, hold))

        # S4/S5 both unpair combinations
        for combo in ('dvr', 'home'):
            n0 = len(sim.events())
            h0 = len(sim.hostlines())
            res = ctl.unpair('Hopper-A', combo=combo)
            time.sleep(1.5)
            ev = sim.events()[n0:]
            hl = sim.hostlines()[h0:]
            downs = [e for e in ev if e['act'] == 'down' and e['result'] == 'confirmed']
            check('unpair(%s): application drove SAT then %s+GUIDE and released all'
                  % (combo, combo.upper()),
                  bool(res.get('ok')) and len(downs) >= 3 and
                  any('86 allup' in l for l in hl),
                  'downs=%d hostlines=%s' % (len(downs), hl))
            check('unpair(%s) reports serial delivery only, not receiver confirmation' % combo,
                  res.get('delivery') == 'serial_flushed')

        # S6 release-all
        h0 = len(sim.hostlines())
        ctl.dart('Hopper-A', 'allup', 'allup')
        time.sleep(1.0)
        check('application release-all emitted the documented Format-B allup',
              any(re.fullmatch(r'1 86 allup', l) for l in sim.hostlines()[h0:]),
              str(sim.hostlines()[h0:]))

        # S7 shared reset
        f0 = len(sim.fwout())
        h0 = len(sim.hostlines())
        ctl.dart('Hopper-A', 'reset', 'reset')
        time.sleep(1.5)
        after = sim.fwout()[f0:]
        check('application shared reset emitted Format-B 99 reset',
              any(re.fullmatch(r'1 99 reset', l) for l in sim.hostlines()[h0:]))
        check('firmware treats it as the SHARED line and reconciles all channels',
              any('SHARED reset line: ALL 16 remotes reboot' in l for l in after) and
              any('all held-key state cleared' in l for l in after),
              str(after))

        # S8 multi-settop commands sharing ONE board
        client = app.test_client()
        h0 = len(sim.hostlines())
        r = client.post('/api/vision/commands/batch',
                        json={'aliases': ['Hopper-A', 'Joey-A', 'Joey-B'],
                              'button': 'reset'})
        time.sleep(2.0)
        body = r.get_json() or {}
        issued = [l for l in sim.hostlines()[h0:] if re.fullmatch(r'\d+ 99 reset', l)]
        check('batch across 3 settops sharing one board returned per-target results',
              r.status_code == 200 and len(body.get('results', [])) == 3,
              'status=%s body=%s' % (r.status_code, body))
        check('batch produced one reset per selected settop, serialised on the shared port',
              len(issued) == 3, str(issued))
        check('batch feedback never claims receiver confirmation',
              all('not confirmed' in (x.get('feedback') or '').lower() or
                  x.get('status') == 'sent' for x in body.get('results', [])),
              str(body.get('results')))
    finally:
        serial_mgr.stop_all()
        sim.stop()

    # ---- honest reporting of failure and uncertainty ----
    # a master that never reads           -> key never delivered -> failed
    # a master that reads but never ACKs  -> delivery unproven   -> uncertain
    for tag, env, remote, alias, expect in (
        ('noread', {'DART_SIM_NOREAD': '2'}, 2, 'Joey-A', 'failed'),
        ('noack', {'DART_SIM_NOACK': '2'}, 2, 'Joey-A', 'uncertain'),
    ):
        sim2 = Sim(tmp, tag, env)
        doc2 = write_base(base, sim2.pty, entries)
        legacy.store.reload()
        legacy.init_serial_from_base(doc2)
        try:
            if not wait_ready(serial_mgr, alias):
                check('fault-injection port ready (%s)' % tag, False)
                continue
            n0 = len(sim2.events())
            ctl.handle_auto_remote('1', alias, 'guide', 120, force='rf')
            time.sleep(4.0)
            ev = [e for e in sim2.events()[n0:] if e['r'] == remote]
            label = 'never reads' if tag == 'noread' else 'never ACKs'
            check('a master that %s is reported as %s, never as success' % (label, expect),
                  any(e['result'] == expect for e in ev) and
                  not any(e['result'] == 'confirmed' for e in ev),
                  str(ev))
        finally:
            serial_mgr.stop_all()
            sim2.stop()

    print('')
    print('=== %d passed, %d failed ===' % (len(PASS), len(FAIL)))
    for f in FAIL:
        print('  FAILED: %s' % f)
    return 1 if FAIL else 0


if __name__ == '__main__':
    sys.exit(main())
