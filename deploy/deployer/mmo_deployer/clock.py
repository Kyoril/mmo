# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Wall clock in the configured time zone; tests substitute FakeClock."""

import datetime
import time
import zoneinfo


class Clock:
	def __init__(self, tz_name):
		self.tz = zoneinfo.ZoneInfo(tz_name)

	def now(self):
		return datetime.datetime.now(self.tz)

	def sleep(self, seconds):
		time.sleep(seconds)
