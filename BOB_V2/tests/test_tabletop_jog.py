import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "tabletop_jog", Path(__file__).parents[1] / "tools" / "tabletop_jog.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class JogPreflight(unittest.TestCase):
    def setUp(self):
        self.pose = [80, 65, 90, 45]
        self.state = dict(armed=False, pid=False, playing=False,
                          commanded=self.pose[:], imuReady=True, calibrated=True,
                          sampleAgeMs=20, pitch=1.2, roll=-.3)

    def test_valid_stationary_pose(self):
        module.check_state(self.state, self.pose, [1, 0])

    def test_recorded_imu_spike_blocks_trial(self):
        self.state.update(pitch=-58.43685, roll=-179.24527)
        with self.assertRaisesRegex(RuntimeError, "Tilt exceeds"):
            module.check_state(self.state, self.pose)

    def test_stale_calibration_competing_owner_and_pose_change(self):
        for patch in ({"sampleAgeMs": 251}, {"calibrated": False}, {"armed": True},
                      {"commanded": [80, 65, 90, 46]}, {"pitch": float("nan")}):
            with self.subTest(patch=patch), self.assertRaises(RuntimeError):
                module.check_state({**self.state, **patch}, self.pose)

    def test_small_trial_relative_tilt_limit(self):
        self.state["pitch"] = 5
        with self.assertRaisesRegex(RuntimeError, "more than 3"):
            module.check_state(self.state, self.pose, [1, 0])


if __name__ == "__main__":
    unittest.main()
