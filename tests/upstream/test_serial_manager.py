from __future__ import annotations

import threading
import time

from jamboree import serial_manager


class FakeSerial:
    def __init__(self, fail=False):
        self.is_open = True
        self.port = "COM1"
        self.fail = fail
        self.writes = []

    def write(self, data):
        if self.fail:
            raise OSError("wire failed")
        self.writes.append(bytes(data))

    def flush(self):
        if self.fail:
            raise OSError("flush failed")

    def close(self):
        self.is_open = False


def test_worker_does_not_shadow_thread_stop():
    worker = serial_manager.SerialPortWorker("COM1")
    assert not isinstance(getattr(worker, "_stop", None), threading.Event)
    assert isinstance(worker._stop_event, threading.Event)


def test_submit_waits_for_actual_serial_write():
    worker = serial_manager.SerialPortWorker("COM1")
    worker._ser = FakeSerial()
    worker._ready.set()
    result = {}

    def submitter():
        result["ok"] = worker.submit(b"1 4 down\n", completion_timeout_s=1)

    thread = threading.Thread(target=submitter)
    thread.start()
    for _ in range(100):
        if not worker._write_q.empty():
            break
        time.sleep(0.001)
    assert worker._drain_writes() is True
    thread.join(1)
    assert result["ok"] is True
    assert worker._ser.writes == [b"1 4 down\n"]


def test_submit_reports_write_failure():
    worker = serial_manager.SerialPortWorker("COM1")
    worker._ser = FakeSerial(fail=True)
    worker._ready.set()
    result = {}

    def submitter():
        result["ok"] = worker.submit(b"bad\n", completion_timeout_s=1)

    thread = threading.Thread(target=submitter)
    thread.start()
    for _ in range(100):
        if not worker._write_q.empty():
            break
        time.sleep(0.001)
    assert worker._drain_writes() is False
    thread.join(1)
    assert result["ok"] is False
    assert worker.last_error == "wire failed"



def test_serial_traffic_is_logged_at_info(caplog):
    import logging
    caplog.set_level(logging.INFO, logger='serial-manager')
    worker = serial_manager.SerialPortWorker('COM1')
    device = FakeSerial()
    worker._ser = device
    worker._ready.set()
    assert worker.submit(b'1 4 down\n', wait=False)
    assert worker._drain_writes()
    assert "TX -> b'1 4 down\\n'" in caplog.text
    assert 'TX flushed (9 bytes)' in caplog.text
    def read():
        worker._stop_event.set()
        return b'board OK\r\n'
    device.readline = read
    worker.run()
    assert "RX <- b'board OK\\r\\n'" in caplog.text


def test_stop_during_read_closes_only_on_worker_thread():
    worker = serial_manager.SerialPortWorker('COM1')
    reading, cancelled = threading.Event(), threading.Event()
    class BlockingSerial(FakeSerial):
        def readline(self):
            reading.set()
            assert cancelled.wait(1)
            assert self.is_open  # stop must not close under the reader
            return b''
        def cancel_read(self):
            cancelled.set()
        def close(self):
            assert threading.current_thread() is worker
            super().close()
    device = BlockingSerial()
    worker._ser = device
    worker._ready.set()
    worker.start()
    assert reading.wait(1)
    worker.stop()
    worker.join(1)
    assert not worker.is_alive() and not device.is_open
    assert not worker.submit(b'late', require_ready=False, wait=False)


def test_read_failure_reopens_port(monkeypatch, caplog):
    worker = serial_manager.SerialPortWorker('COM1')
    broken, replacement = FakeSerial(), FakeSerial()
    def bad_read():
        raise TypeError("'NoneType' object cannot be interpreted as an integer")
    broken.readline = bad_read
    def good_read():
        worker._stop_event.set()
        return b'back online\n'
    replacement.readline = good_read
    worker._ser = broken
    def reopen():
        worker._ser = replacement
        return True
    monkeypatch.setattr(worker, '_open_serial', reopen)
    worker.run()
    assert not broken.is_open and not replacement.is_open
    assert 'read failed' in caplog.text


def test_idle_port_does_not_toggle_dtr_and_still_sends(monkeypatch):
    worker = serial_manager.SerialPortWorker('COM1')
    class QuietSerial(FakeSerial):
        def __init__(self):
            super().__init__()
            self.reads = 0
            self.dtr_changes = []
        @property
        def dtr(self):
            return True
        @dtr.setter
        def dtr(self, value):
            self.dtr_changes.append(value)
        def readline(self):
            self.reads += 1
            if self.reads == 2:
                assert worker.submit(b'2 83 03 120\n',wait=False)
            if self.reads == 3:
                worker._stop_event.set()
            return b''
    device = QuietSerial()
    worker._ser = device
    worker._ready.set()
    worker._last_activity = -1000
    monkeypatch.setattr(serial_manager.time,'monotonic',lambda:1000000)
    worker.run()
    assert device.dtr_changes == []
    assert device.writes == [b'2 83 03 120\n']
    assert device.reads == 3 and not device.is_open


def test_open_preserves_boot_banner_in_rx_log(monkeypatch,caplog):
    import logging
    worker = serial_manager.SerialPortWorker('COM1')
    class BootSerial(FakeSerial):
        def __init__(self):
            super().__init__()
            self.boot = b'11.05.2024 fixture DART version\r\n'
            self.cleared_input = False
        def reset_input_buffer(self):
            self.cleared_input = True
            self.boot = b''
        def reset_output_buffer(self):
            pass
        def readline(self):
            worker._stop_event.set()
            return self.boot
    device = BootSerial()
    monkeypatch.setattr(serial_manager.serial,'Serial',lambda **kwargs:device)
    monkeypatch.setattr(worker._stop_event,'wait',lambda timeout:False)
    assert worker._open_serial()
    caplog.set_level(logging.INFO,logger='serial-manager')
    worker.run()
    assert not device.cleared_input
    assert 'fixture DART version' in caplog.text
