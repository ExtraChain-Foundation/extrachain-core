"""Exercise repeated C API initialization and shutdown in a fresh data directory."""
import ctypes
import os
import sys
import tempfile
import threading
import time

library = ctypes.CDLL(os.path.abspath(sys.argv[1]))
library.exc_init.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_uint16]
library.exc_init.restype = ctypes.c_int
library.exc_shutdown.restype = ctypes.c_int
library.exc_is_initialized.restype = ctypes.c_bool
ready_type = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
library.exc_on_node_ready.argtypes = [ready_type, ctypes.c_void_p]
original = os.getcwd()
with tempfile.TemporaryDirectory(prefix='extrachain-cbindings-') as directory:
    os.chdir(directory)
    try:
        for iteration in range(3):
            assert library.exc_init(0, None, 18991) == 0, iteration
            assert library.exc_is_initialized(), iteration
            assert library.exc_shutdown() == 0, iteration
            assert not library.exc_is_initialized(), iteration
        initialized = threading.Event()
        finished = threading.Event()
        results = []

        @ready_type
        def shutdown_from_callback(_):
            if not initialized.wait(10):
                results.append('initialization timeout')
            else:
                results.append(library.exc_shutdown())
            finished.set()

        library.exc_on_node_ready(shutdown_from_callback, None)
        assert library.exc_init(0, None, 18991) == 0
        initialized.set()
        assert finished.wait(15), 'ready callback did not finish'
        assert results == [0], results
        library.exc_on_node_ready(ready_type(), None)
        deadline = time.monotonic() + 15
        while True:
            result = library.exc_init(0, None, 18991)
            if result == 0:
                break
            assert time.monotonic() < deadline, ('callback shutdown did not complete', result)
            time.sleep(0.05)
        assert library.exc_shutdown() == 0
    finally:
        os.chdir(original)
print('C API lifecycle: PASS')
