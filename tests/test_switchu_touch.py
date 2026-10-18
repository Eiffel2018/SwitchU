import queue
import socket
import sys
import time
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).parent))
from test_switchu_remote import MockSwitchU, remote
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/breezepc"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from pcconnect_app import App


class Var:
    def __init__(self, value): self.value = value
    def get(self): return self.value
    def set(self, value): self.value = value


class Root:
    def __init__(self): self.jobs = {}; self.next = 0
    def after(self, ms, fn):
        self.next += 1; self.jobs[self.next] = fn; return self.next
    def after_cancel(self, key): self.jobs.pop(key, None)
    def flush(self):
        jobs = list(self.jobs.values()); self.jobs.clear()
        for fn in jobs: fn()


class TouchTests(unittest.TestCase):
    def server(self, **kw):
        s = MockSwitchU(**kw); self.addCleanup(s.close); return s

    def test_wire_modes_edges_and_long_timeout(self):
        for gesture, x, y, ms, gap in (("tap",0,719,100,0), ("double",1279,0,80,120), ("long",640,360,5000,0)):
            reply = "OK touch %s x=%d y=%d ms=%d gap=%d" % (gesture,x,y,ms,gap)
            s = self.server(command_reply=reply)
            with patch.object(socket, "create_connection", wraps=socket.create_connection) as connect:
                self.assertEqual(remote.touch("127.0.0.1","pair",x,y,gesture=gesture,duration_ms=ms,port=s.port),reply)
                self.assertGreaterEqual(connect.call_args.kwargs['timeout'],ms/1000+2)
            self.assertEqual(s.commands,["touch %s %d %d %d"%(gesture,x,y,ms)+(" %d"%gap if gesture=="double" else "")])

    def test_unknown_touch_is_never_replayed(self):
        for kw in ({'close_after_command':True},{'command_reply':'OK touch tap x=1 y=2 ms=100 gap=0 extra'}):
            s=self.server(**kw)
            with self.assertRaises(remote.AmbiguousInputError):remote.touch('127.0.0.1','pair',1,2,port=s.port)
            self.assertEqual(s.commands,['touch tap 1 2 100'])

    def test_invalid_touch_never_connects(self):
        for args in (('tap',1280,0,100),('tap',0,720,100),('tap',-1,0,100),('tap',0,0,301),('long',0,0,299),('long',0,0,5001),('double',0,0,0),('swipe',0,0,100),('tap',True,0,100)):
            with patch.object(socket,'create_connection') as connect:
                with self.assertRaises(ValueError):remote.touch('host','pair',args[1],args[2],gesture=args[0],duration_ms=args[3])
                connect.assert_not_called()

    def app(self):
        a=object.__new__(App);a.root=Root();a.switchu_touch_preview=Var(True);a.switchu_touch_gesture=Var('tap')
        a.switchu_touch_after=None;a.switchu_touch_down=None;a.view_w=640;a.view_h=360
        a.status=SimpleNamespace(config=lambda **kw:None);a.calls=[]
        a.send_switchu_touch=lambda *args:a.calls.append(args)
        return a

    def test_preview_single_double_long_and_letterbox(self):
        a=self.app();e=SimpleNamespace(x=320,y=180)
        a._touch_down_preview(e);a.on_touch_release(e);self.assertEqual(a.calls,[])
        a._touch_down_preview(e,double=True);a.on_touch_release(e);a.root.flush()
        self.assertEqual(a.calls,[(640,360,'double',100)])
        a.calls.clear();a._touch_down_preview(e);x,y,t,d=a.switchu_touch_down
        a.switchu_touch_down=(x,y,t-1,d);a.on_touch_release(e);self.assertEqual(a.calls[0][:3],(640,360,'long'))
        self.assertGreaterEqual(a.calls[0][3],1000)
        a.calls.clear();a._touch_down_preview(SimpleNamespace(x=650,y=100));a.on_touch_release(e);self.assertEqual(a.calls,[])
        a._touch_down_preview(SimpleNamespace(x=0,y=0));a.on_touch_release(SimpleNamespace(x=0,y=0));a.root.flush()
        self.assertEqual(a.calls,[(0,0,'tap',100)])

    def test_pending_tap_is_cancelled_on_off_and_mode_change(self):
        a=self.app();e=SimpleNamespace(x=10,y=10);a._touch_down_preview(e);a.on_touch_release(e)
        a._cancel_touch_preview();a.root.flush();self.assertEqual(a.calls,[])
        a._touch_down_preview(e);a.on_touch_release(e);a.switchu_touch_preview.set(False);a.root.flush();self.assertEqual(a.calls,[])
        a.switchu_touch_preview.set(True);a.switchu_touch_ms=Var('100')
        a._touch_down_preview(e);a.on_touch_release(e);a._set_switchu_touch_gesture('long');a.root.flush()
        self.assertEqual(a.calls,[]);self.assertEqual(a.switchu_touch_ms.get(),'800')

    def test_disabled_gate_rejects_touch_without_worker(self):
        a=object.__new__(App);a.switchu_controller_state='disabled';a.switchu_controller_command_pending=False
        a.status=SimpleNamespace(config=lambda **kw:None)
        with patch('pcconnect_app.threading.Thread') as thread:
            App.send_switchu_touch(a,1,2,'tap',100);thread.assert_not_called()


if __name__=='__main__':unittest.main()
