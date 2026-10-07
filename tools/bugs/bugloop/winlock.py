# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Named Windows mutexes: the gate lock shared with tools/gate (Local\\MMOGateWorktree, so the
loop never runs E2E alongside the nightly gate) and the loop's single-instance lock. No-ops
off Windows."""

import contextlib
import ctypes
import sys

GATE_MUTEX = "Local\\MMOGateWorktree"
_WAIT_OBJECT_0 = 0x0
_WAIT_ABANDONED = 0x80
_INFINITE = 0xFFFFFFFF


def _kernel32():
	kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
	kernel32.CreateMutexW.restype = ctypes.c_void_p
	kernel32.CreateMutexW.argtypes = [ctypes.c_void_p, ctypes.c_bool, ctypes.c_wchar_p]
	kernel32.WaitForSingleObject.restype = ctypes.c_uint32
	kernel32.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
	kernel32.ReleaseMutex.argtypes = [ctypes.c_void_p]
	kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
	return kernel32


def _create(kernel32, name):
	handle = kernel32.CreateMutexW(None, False, name)
	if not handle:
		raise OSError(ctypes.get_last_error(), "CreateMutexW failed for " + name)
	return handle


@contextlib.contextmanager
def named_mutex(name=GATE_MUTEX):
	"""Blocks until the mutex is ours. An abandoned mutex (holder died) counts as acquired."""
	if sys.platform != "win32":
		yield
		return
	kernel32 = _kernel32()
	handle = _create(kernel32, name)
	try:
		result = kernel32.WaitForSingleObject(handle, _INFINITE)
		if result not in (_WAIT_OBJECT_0, _WAIT_ABANDONED):
			raise OSError(ctypes.get_last_error(), "WaitForSingleObject failed for " + name)
		try:
			yield
		finally:
			kernel32.ReleaseMutex(handle)
	finally:
		kernel32.CloseHandle(handle)


def acquire_single_instance(name):
	"""A handle while no other process holds `name`, else None. Keep the handle for the
	process lifetime; Windows releases it at exit."""
	if sys.platform != "win32":
		return object()
	kernel32 = _kernel32()
	handle = _create(kernel32, name)
	if kernel32.WaitForSingleObject(handle, 0) in (_WAIT_OBJECT_0, _WAIT_ABANDONED):
		return handle
	kernel32.CloseHandle(handle)
	return None
