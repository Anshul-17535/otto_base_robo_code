import importlib.util
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "motion_client", Path(__file__).parents[1] / "tools" / "motion_client.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class FakeClient(module.MotionClient):
    def __init__(self):
        super().__init__("127.0.0.1")
        self.calls = []

    def request(self, path, payload=None):
        self.calls.append((path, payload))
        if path == "/motion/arm":
            return {"token": "test-token"}
        return {"ok": True}


class ClientLifecycle(unittest.TestCase):
    def test_inspect_never_arms_or_mutates(self):
        robot = FakeClient()
        self.assertEqual(len(robot.inspect()), 5)
        self.assertTrue(all(body is None for _, body in robot.calls))

    def test_exception_stops_owned_session(self):
        robot = FakeClient()
        with self.assertRaisesRegex(RuntimeError, "trial failed"):
            with robot:
                robot.command("pid", enabled=False, target=[0, 0])
                raise RuntimeError("trial failed")
        self.assertEqual(robot.calls[-1], ("/motion/stop", {"token": "test-token"}))
        self.assertIsNone(robot.token)
        self.assertFalse(robot._thread.is_alive())

    def test_unarmed_command_is_rejected_locally(self):
        robot = FakeClient()
        with self.assertRaises(RuntimeError):
            robot.command("play", sequence={}, repeats=1)
        self.assertEqual(robot.calls, [])

    def test_lost_heartbeat_blocks_further_commands(self):
        robot = FakeClient()
        with robot:
            robot.error = OSError("offline")
            with self.assertRaises(RuntimeError):
                robot.command("pid", enabled=True, target=[0, 0])
        self.assertFalse(any(path == "/motion/pid" for path, _ in robot.calls))


if __name__ == "__main__":
    unittest.main()
