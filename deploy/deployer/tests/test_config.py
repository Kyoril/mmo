# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

import datetime
import unittest

from mmo_deployer.config import ConfigError, load_config

from .fakes import BASE_ENV


class LoadConfig(unittest.TestCase):
	def test_defaults(self):
		cfg = load_config(BASE_ENV)
		self.assertEqual(cfg.maintenance_at, datetime.time(4, 45))
		self.assertEqual(cfg.countdown_s, 900)
		self.assertEqual(cfg.poll_interval_s, 900)
		self.assertEqual(cfg.keep_releases, 3)
		self.assertEqual(cfg.keep_backups, 7)
		self.assertEqual(cfg.release_prefix, "nightly-")
		self.assertEqual(cfg.timezone, "Europe/Berlin")
		self.assertEqual(cfg.wait_services, ("realm_server_01", "world_node_01"))
		self.assertEqual(cfg.backup_databases, ("mmo_login", "mmo_realm_01"))
		self.assertEqual(cfg.portainer_stack_id, 7)
		self.assertEqual(cfg.notify_format, "discord")
		self.assertFalse(cfg.dry_run)

	def test_realm_urls_lose_trailing_slash(self):
		cfg = load_config(BASE_ENV)
		self.assertEqual(cfg.realms[0].url, "http://mmo-dev-realm-01:8092")
		self.assertEqual(cfg.realms[0].name, "realm01")

	def test_missing_variables_are_all_named(self):
		env = dict(BASE_ENV)
		del env["PORTAINER_TOKEN"]
		del env["REALMS"]
		with self.assertRaises(ConfigError) as ctx:
			load_config(env)
		self.assertIn("PORTAINER_TOKEN", str(ctx.exception))
		self.assertIn("REALMS", str(ctx.exception))

	def test_bad_realms_json(self):
		with self.assertRaises(ConfigError):
			load_config(dict(BASE_ENV, REALMS="not json"))

	def test_empty_realms(self):
		with self.assertRaises(ConfigError):
			load_config(dict(BASE_ENV, REALMS="[]"))

	def test_overrides(self):
		cfg = load_config(dict(BASE_ENV, MAINTENANCE_AT="05:30", DRY_RUN="1", NOTIFY_FORMAT="slack"))
		self.assertEqual(cfg.maintenance_at, datetime.time(5, 30))
		self.assertTrue(cfg.dry_run)
		self.assertEqual(cfg.notify_format, "slack")

	def test_bad_values(self):
		for key, value in (("MAINTENANCE_AT", "25:99"), ("COUNTDOWN_S", "soon"), ("NOTIFY_FORMAT", "pigeon")):
			with self.subTest(key=key):
				with self.assertRaises(ConfigError):
					load_config(dict(BASE_ENV, **{key: value}))


if __name__ == "__main__":
	unittest.main()
