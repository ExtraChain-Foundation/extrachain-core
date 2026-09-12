"""Round-trip a profile backup through the text-based C API."""
import base64
import ctypes
import os
from pathlib import Path
import sys
import tempfile


class Mnemonic(ctypes.Structure):
    _fields_ = [(name, ctypes.c_void_p) for name in ("phrase", "main_id", "profile_id")]


library = ctypes.CDLL(os.path.abspath(sys.argv[1]))
library.exc_init.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_uint16]
library.exc_init.restype = ctypes.c_int
library.exc_shutdown.restype = ctypes.c_int
library.exc_profile_create.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(Mnemonic)]
library.exc_profile_create.restype = ctypes.c_int
library.exc_mnemonic_free.argtypes = [ctypes.POINTER(Mnemonic)]
library.exc_string_free.argtypes = [ctypes.c_void_p]
library.exc_profile_export.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
library.exc_profile_export.restype = ctypes.c_int
for name in ("exc_profile_import_data", "exc_profile_import_file"):
    function = getattr(library, name)
    function.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
    function.restype = ctypes.c_int
original = Path.cwd()
with tempfile.TemporaryDirectory(prefix="extrachain-profile-backup-") as directory:
    os.chdir(directory)
    initialized = False
    try:
        assert library.exc_init(0, None, 0) == 0
        initialized = True
        mnemonic = Mnemonic()
        try:
            assert library.exc_profile_create(b"backup-test", b"test-password", ctypes.byref(mnemonic)) == 0
            assert mnemonic.main_id and mnemonic.profile_id and mnemonic.phrase
        finally:
            library.exc_mnemonic_free(ctypes.byref(mnemonic))
        output = ctypes.c_void_p()
        assert library.exc_profile_export(ctypes.byref(output)) == 0
        try:
            exported = ctypes.string_at(output)
        finally:
            library.exc_string_free(output)
        ciphertext = base64.urlsafe_b64decode(exported + b"=" * (-len(exported) % 4))
        assert len(ciphertext) >= 48
        backup = Path("profile.backup")
        backup.write_bytes(exported)
        for function, data in ((library.exc_profile_import_data, exported),
                               (library.exc_profile_import_file, os.fsencode(backup))):
            rejected = ctypes.c_void_p()
            assert function(data, b"backup-test", b"wrong-password", ctypes.byref(rejected)) != 0
            assert not rejected.value
            restored = ctypes.c_void_p()
            assert function(data, b"backup-test", b"test-password", ctypes.byref(restored)) == 0
            try:
                assert ctypes.string_at(restored)
            finally:
                library.exc_string_free(restored)
        assert len(ciphertext) == 98 and ciphertext.startswith(b"ECP2")
    finally:
        if initialized:
            assert library.exc_shutdown() == 0
        os.chdir(original)
