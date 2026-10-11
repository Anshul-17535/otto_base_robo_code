"""Robot ownership, full text and RPS restoration without hardware access."""
import sys
import threading
import unittest
from pathlib import Path
from unittest.mock import Mock, patch
sys.path[:0] = [str(Path(__file__).resolve().parents[1]),
                str(Path(__file__).resolve().parents[1] / 'lib')]
import server
import robo_robot as robot

class ActivityDisplayTests(unittest.TestCase):
    def test_pending_foreground_drains_idle_and_blocks_new_idle(self):
        entered = threading.Event()
        release = threading.Event()
        def foreground():
            with robot.foreground_activity():
                entered.set()
                release.wait(2)
        with robot.idle_activity() as acquired:
            self.assertTrue(acquired)
            worker = threading.Thread(target=foreground)
            worker.start()
            self.assertTrue(robot.FOREGROUND_BUSY.wait(2))
            self.assertFalse(entered.is_set())
            with robot.idle_activity() as second:
                self.assertFalse(second)
        try:
            self.assertTrue(entered.wait(2))
            with robot.idle_activity() as second:
                self.assertFalse(second)
        finally:
            release.set()
            worker.join(2)
        self.assertFalse(worker.is_alive())
        self.assertFalse(robot.FOREGROUND_BUSY.is_set())

    def test_full_reply_pages_not_llm_summary(self):
        text = 'This entire spoken reply must remain readable on the robot screen even when it is much longer than one hundred and eight characters.'
        stop = Mock()
        stop.is_set.return_value = False
        state = Mock(stop_event=stop)
        def speak(text, **kwargs):
            kwargs['before_audio']()
            return True
        with patch.object(server, '_state', state), patch.object(server._tts, 'robot_speak', side_effect=speak), patch.object(robot, 'set_expression'), patch.object(robot, 'show_screen'), patch.object(robot, 'robot_ticker') as ticker:
            self.assertTrue(server._speak_reply(text, ticker='short summary'))
        pages = [call.args[0] for call in ticker.call_args_list]
        self.assertEqual(' '.join(pages), text)
        self.assertTrue(all(len(page) <= 26 for page in pages))
        self.assertEqual(stop.wait.call_count, len(pages))

    def test_display_normalizes_punctuation(self):
        self.assertEqual(server._display_pages('“I’m ready…”'), ['"I\'m ready..."'])

    def test_game_owns_countdown_and_restores_face_on_error(self):
        def game(stop):
            self.assertTrue(robot.FOREGROUND_BUSY.is_set())
            with robot.idle_activity() as acquired:
                self.assertFalse(acquired)
            raise RuntimeError('failed reveal')
        with patch.object(server, '_run_rps_demo', side_effect=game), patch.object(robot, 'set_expression') as eyes, patch.object(robot, 'show_screen') as screen:
            with self.assertRaises(RuntimeError):
                server._play_rps_demo()
            eyes.assert_called_once_with('normal')
            screen.assert_called_once_with('idle')
        self.assertFalse(robot.FOREGROUND_BUSY.is_set())

    def test_game_reveal_resets_face_and_sends_no_moves(self):
        stop = Mock()
        stop.is_set.return_value = False
        events = []
        with patch.object(server, '_speak_reply', side_effect=lambda *a: events.append(('reply', a[1]))), patch.object(server._tts, 'robot_speak'), patch.object(robot, 'robot_ticker') as ticker, patch.object(robot, 'rps_reveal', side_effect=lambda *a: events.append(('reveal', a[0]))), patch.object(robot, 'set_expression', side_effect=lambda *a: events.append(('face', a[0]))), patch.object(robot, 'show_screen'), patch.object(server, '_play_named_move') as move, patch.object(server.time, 'sleep'):
            server._play_rps_demo(stop)
        self.assertIn('SHOOT!', [c.args[0] for c in ticker.call_args_list])
        self.assertLess(events.index(('reveal', 'paper')), events.index(('face', 'normal')))
        self.assertLess(events.index(('face', 'normal')), events.index(('reply', 'happy')))
        move.assert_not_called()
