"""Check that the C API returns valid JSON for every control byte in DFS names."""
import base64
import ctypes
import json
import os
from pathlib import Path
import sqlite3
import sys
import tempfile


library = ctypes.CDLL(os.path.abspath(sys.argv[1]))
library.exc_init.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_uint16]
library.exc_init.restype = ctypes.c_int
library.exc_shutdown.restype = ctypes.c_int
library.exc_dfs_list_files.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
library.exc_dfs_list_files.restype = ctypes.c_int
library.exc_string_free.argtypes = [ctypes.c_void_p]
original = Path.cwd()
with tempfile.TemporaryDirectory(prefix="extrachain-dfs-json-") as directory:
    os.chdir(directory)
    initialized = False
    try:
        assert library.exc_init(0, None, 0) == 0
        initialized = True
        owner = "a" * 40
        file_id = "b" * 64
        name = "file" + "".join(chr(code) for code in range(1, 32)) + '\0"\\'
        folder = "folder" + "".join(chr(code) for code in range(1, 32)) + "\0end"
        signature = base64.urlsafe_b64encode(bytes(64)).decode().rstrip("=")
        with sqlite3.connect("dfs/.dirs") as database:
            database.execute(
                "INSERT INTO ActorsFiles(owner_id, file_id, actor_id, hash, folder, name, "
                "size, created, last_modified, type, encryption, state, sign) "
                "VALUES(?, ?, ?, ?, ?, ?, 123, 456, 456, 0, 0, 2, ?)",
                (owner, file_id, owner, "c" * 64, folder, name, signature),
            )
        output = ctypes.c_void_p()
        assert library.exc_dfs_list_files(owner.encode(), ctypes.byref(output)) == 0
        assert output.value
        try:
            rows = json.loads(ctypes.string_at(output).decode())
        finally:
            library.exc_string_free(output)
        assert rows == [{"file_id": file_id, "name": name, "folder": folder,
                         "size": 123, "created": 456, "type": 0, "encrypted": False}], rows
    finally:
        if initialized:
            assert library.exc_shutdown() == 0
        os.chdir(original)
print("C API DFS JSON: PASS")
