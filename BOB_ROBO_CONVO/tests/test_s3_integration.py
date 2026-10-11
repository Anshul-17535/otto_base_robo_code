"""Contract checks with no robot, microphone, model downloads or speech output."""
import io
import json
import os
import sys
import tempfile
import unittest
import wave
from pathlib import Path
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / "lib")]
import server
import robo_robot as robot
import robo_tts as tts
import robo_vad as vad


def wav_bytes(frames=16000):
    stream = io.BytesIO()
    with wave.open(stream, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(16000)
        wav.writeframes(b'\x01\x00' * frames)
    return stream.getvalue()


class AudioContract(unittest.TestCase):
    def test_robot_capture_clamps_duration_and_validates_wav(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "input.wav"
            with patch.object(robot, 'robot_request', return_value=Mock(content=wav_bytes())) as request:
                self.assertTrue(robot.capture_audio(str(target), 30))
                self.assertEqual(request.call_args.kwargs['params'], {'secs': 3})
                self.assertEqual(target.read_bytes(), wav_bytes())
            for invalid in (b'not audio', wav_bytes()[:-10], wav_bytes(0)):
                with patch.object(robot, 'robot_request', return_value=Mock(content=invalid)):
                    self.assertFalse(robot.capture_audio(str(target)))

    def test_input_switch_selects_correct_capture_path(self):
        with patch.dict(os.environ, {'MIC_SOURCE': 'robot', 'ROBOT_RECORD_SECS': '9'}):
            with patch.object(robot, 'capture_audio', return_value=True) as record:
                self.assertTrue(vad.capture_until_silence('sample.wav'))
                record.assert_called_once_with('sample.wav', 3)
        with patch.dict(os.environ, {'MIC_SOURCE': 'mac'}):
            with patch.object(vad, '_capture_mac_until_silence', return_value=True) as record:
                self.assertTrue(vad.capture_until_silence('sample.wav'))
                record.assert_called_once()

    def test_tts_upload_has_exact_firmware_header_and_multipart(self):
        pcm = b'\x20\x00' * 16000
        def upload(method, path, **kwargs):
            self.assertEqual((method, path), ('POST', '/mactts'))
            name, audio, mime = kwargs['files']['f']
            data = audio.read()
            self.assertEqual((name, mime), ('tts.wav', 'audio/wav'))
            self.assertEqual(data[36:40], b'data')
            self.assertEqual(data[44:], pcm)
            with wave.open(io.BytesIO(data)) as wav:
                self.assertEqual((wav.getnchannels(), wav.getframerate(), wav.getsampwidth()), (1, tts._RATE, 2))
            return Mock()
        with patch.object(tts.subprocess, 'run', side_effect=[Mock(), Mock(stdout=pcm)]):
            with patch.object(tts, 'robot_request', side_effect=upload) as request:
                self.assertTrue(tts.robot_speak('Hello Bob'))
                request.assert_called_once()

    def test_disabled_imu_sends_no_requests(self):
        with patch.object(robot, 'IMU_ENABLED', False), patch.object(robot, 'robot_request') as request:
            robot.liftdetect_on(True)
            robot.helpme_on(True)
            request.assert_not_called()

    def test_http_failure_is_not_online(self):
        with patch.object(robot.requests, 'request', return_value=Mock(ok=False, status_code=503, text='starting up')):
            self.assertFalse(robot.robot_status()['online'])


class ControlAPI(unittest.TestCase):
    def post(self, path, payload):
        handler = server._Handler.__new__(server._Handler)
        body = json.dumps(payload).encode()
        handler.path = path
        handler.headers = {'Content-Length': str(len(body))}
        handler.rfile = io.BytesIO(body)
        handler._send_json = Mock()
        handler.do_POST()
        return handler._send_json.call_args.args

    def test_mic_switch_and_invalid_choice(self):
        with patch.dict(os.environ, {'MIC_SOURCE': 'robot'}), patch.object(vad, 'recalibrate'):
            self.assertEqual(self.post('/mic', {'source': 'mac'})[0], 200)
            self.assertEqual(os.environ['MIC_SOURCE'], 'mac')
            self.assertEqual(self.post('/mic', {'source': 'unknown'})[0], 400)
            self.assertEqual(os.environ['MIC_SOURCE'], 'mac')
            self.assertEqual(self.post('/mic', ['robot'])[0], 400)
            self.assertEqual(self.post('/mic', {'source': 'robot'})[0], 200)

    def test_repeated_start_does_not_duplicate_conversation(self):
        with patch.object(server, '_state', server.ModeState()), patch.object(server, '_launch_convo') as launch:
            self.post('/mode/convo', {})
            self.post('/mode/convo', {})
            launch.assert_called_once()


if __name__ == '__main__':
    unittest.main()
