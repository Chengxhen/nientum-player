from __future__ import annotations

import math
import time
from pathlib import Path

import numpy as np
from PySide6.QtCore import QObject, QTimer, QUrl, Signal
from PySide6.QtMultimedia import QAudioOutput, QMediaDevices, QMediaPlayer, QSoundEffect

from . import applog, paths


class AudioEngine(QObject):
    positionChanged = Signal(int)
    durationChanged = Signal(int)
    stateChanged = Signal()
    finished = Signal()
    previewEnded = Signal()
    error = Signal(str)

    def __init__(self) -> None:
        super().__init__()
        self.player = QMediaPlayer()
        self.out = QAudioOutput()
        self.player.setAudioOutput(self.out)
        self._base_volume = 0.78
        self.out.setVolume(self._base_volume)
        self.track = None
        self.previewing = False
        self.full_mode = False
        self._preview_stop = 0
        self._preview_pending = False
        self._suspended = None
        self._handover = None
        self._resume_on_fade = True
        self._preview_fade_in = 1.0
        self._preview_fade_out = 1.2
        self._spec = None
        self._spec_id = ""
        self._last_error = ""
        # preview volume fades
        self._fade = 1.0
        self._fade_from = 1.0
        self._fade_to = 1.0
        self._fade_t0 = 0.0
        self._fade_dur = 1.0
        self._fade_done = None
        self._fade_timer = QTimer(self)
        self._fade_timer.setInterval(30)
        self._fade_timer.timeout.connect(self._fade_step)
        self._timer = QTimer(self)
        self._timer.setInterval(90)
        self._timer.timeout.connect(self._tick)
        self.player.positionChanged.connect(lambda p: self.positionChanged.emit(int(p)))
        self.player.durationChanged.connect(lambda d: self.durationChanged.emit(int(d)))
        self.player.playbackStateChanged.connect(lambda _s: self.stateChanged.emit())
        self.player.mediaStatusChanged.connect(self._status)
        self.player.errorOccurred.connect(self._error)
        # Title music and small UI sounds use their own players so they do
        # not disturb the concert player.
        self.ui_bgm = QMediaPlayer()
        self.ui_bgm_out = QAudioOutput()
        self.ui_bgm.setAudioOutput(self.ui_bgm_out)
        self.ui_bgm_out.setVolume(0.28)
        self._title_track = ""
        self._ui_move = QSoundEffect()
        self._ui_select = QSoundEffect()
        for effect, name in ((self._ui_move, "menu_move.wav"), (self._ui_select, "menu_select.wav")):
            effect.setSource(QUrl.fromLocalFile(str(paths.data("ui", name))))
            effect.setVolume(0.32)
        try:
            self.ui_bgm.setLoops(QMediaPlayer.Loops.Infinite)
        except Exception:
            self.ui_bgm.setLoops(-1)

    def play_title_bgm(self, track) -> None:
        if track is None:
            return
        if self._title_track == track.id and self.ui_bgm.playbackState() == QMediaPlayer.PlaybackState.PlayingState:
            return
        self._title_track = track.id
        self.ui_bgm.stop()
        self.ui_bgm.setSource(QUrl.fromLocalFile(str(track.abs_path)))
        self.ui_bgm.play()

    def stop_title_bgm(self) -> None:
        self.ui_bgm.stop()
        self._title_track = ""

    def play_ui(self, kind: str = "move") -> None:
        effect = self._ui_select if kind == "select" else self._ui_move
        if effect.source().isEmpty():
            return
        effect.play()

    # ------------------------------------------------------------------
    def _emit_preview_end(self) -> None:
        # Leave the output at its faded level.  QMediaPlayer pauses
        # asynchronously, so restoring full volume right after pause() can
        # leak the last buffered samples as a short loud blip.  Every real
        # playback entry point (play / toggle / _restore_paused) applies the
        # saved volume before it starts.
        self._fade_timer.stop()
        self.previewing = False
        self._preview_pending = False
        self._preview_stop = 0
        resume = self._resume_on_fade
        self._resume_on_fade = True
        self.previewEnded.emit()
        if resume:
            self._resume_suspended()

    # -- auditioning without losing the concert ---------------------------
    def suspend_full(self) -> None:
        """Pause the current full playback and remember where it was."""
        if self._suspended is not None:
            return
        if self.full_mode and self.track is not None:
            if self.player.playbackState() != QMediaPlayer.PlaybackState.StoppedState:
                self._suspended = (self.track, int(self.player.position()), self.playing)
                self.player.pause()
                self.stateChanged.emit()

    def play_preview(self, track, start_ms: int = 0, preview_len: int = 20000,
                     fade_over: float = 0.55) -> None:
        """Start an audition, fading a currently playing concert out first."""
        if track is None:
            return
        if self.full_mode and self.track is not None:
            if self.playing and self._fade > 0.01 and fade_over > 0:
                self._handover = (track, int(start_ms), int(preview_len))
                self._start_fade(0.0, fade_over, self._finish_preview_handover)
                return
            self.suspend_full()
        self.play(track, start_ms, preview=True, preview_len=preview_len)

    def _finish_preview_handover(self) -> None:
        req = self._handover
        self._handover = None
        if req is None:
            return
        track, start_ms, preview_len = req
        # The concert is already silent here; park it at this position and
        # let the audition fade in on the same player.
        self.suspend_full()
        self.play(track, start_ms, preview=True, preview_len=preview_len)

    def cancel_handover(self, restore: bool = True) -> bool:
        """Cancel a pending full->preview fade before the preview starts."""
        if self._handover is None:
            return False
        self._handover = None
        if restore and self.full_mode and self.track is not None and self.playing:
            self._start_fade(1.0, 0.35)
        return True

    def drop_suspended(self) -> None:
        self._suspended = None

    def _resume_suspended(self) -> None:
        saved = self._suspended
        self._suspended = None
        if saved is None:
            return
        track, pos, was_playing = saved
        if track is None:
            return
        if was_playing:
            self.play(track, pos, preview=False, fade_in=0.75)
        else:
            self._restore_paused(track, pos)

    def _restore_paused(self, track, pos: int) -> None:
        self._fade_timer.stop()
        self._fade_done = None
        self._fade = 1.0
        self._apply_volume()
        self.player.stop()
        self.track = track
        self.player.setSource(QUrl.fromLocalFile(str(track.abs_path)))
        self._load_spectrum(track)
        self.previewing = False
        self.full_mode = True
        self._preview_pending = False
        self._preview_stop = 0
        self._timer.stop()
        QTimer.singleShot(80, lambda: self.player.setPosition(int(pos)))
        self.stateChanged.emit()

    def _status(self, status) -> None:
        if status == QMediaPlayer.MediaStatus.EndOfMedia:
            self._timer.stop()
            if self.previewing or self._preview_pending:
                self._emit_preview_end()
            else:
                self.full_mode = False
                self.finished.emit()
        elif status == QMediaPlayer.MediaStatus.InvalidMedia:
            applog.log(f"audio invalid media: track={getattr(self.track, 'id', None)} error={self._last_error}")
            self.error.emit("无法读取音频文件")

    def _error(self, error, string: str) -> None:
        # The bundled FLACs have one corrupt frame at the very end.  Treat an
        # error in the final seconds as a normal end of track.
        dur = self.player.duration()
        pos = self.player.position()
        self._last_error = string or str(error)
        if dur > 0 and pos >= dur - 4000:
            self._timer.stop()
            if self.previewing or self._preview_pending:
                self._emit_preview_end()
            else:
                self.full_mode = False
                self.finished.emit()
            return
        if self.previewing or self._preview_pending:
            self._emit_preview_end()
            return
        if "flac" in self._last_error.lower() or "residual" in self._last_error.lower():
            # trailing decoder noise; let the track finish silently
            return
        applog.log(f"audio error: {error} {self._last_error} dur={dur} pos={pos}")
        self.error.emit(self._last_error)

    def _tick(self) -> None:
        if self.previewing and self._preview_stop:
            if self.player.position() >= self._preview_stop:
                # let the last half second fade away instead of cutting off
                self.previewing = False
                self._preview_pending = True
                self._timer.stop()
                self._start_fade(0.0, self._preview_fade_out, self._finish_preview_fade)

    # ------------------------------------------------------------------
    @staticmethod
    def _preview_fade_times(preview_len: int) -> tuple[float, float]:
        secs = max(1.0, preview_len / 1000.0)
        fade_in = min(2.2, max(1.2, 0.9 + secs * 0.04))
        fade_out = min(1.9, max(1.0, 0.8 + secs * 0.035))
        return fade_in, fade_out

    @property
    def playing(self) -> bool:
        return self.player.playbackState() == QMediaPlayer.PlaybackState.PlayingState

    @property
    def preview_active(self) -> bool:
        return self.previewing or self._preview_pending

    def play(self, track, start_ms: int = 0, preview: bool = False,
             preview_len: int = 20000, fade_in: float = 0.0) -> None:
        if track is None:
            return
        changed = self.track is None or self.track.id != track.id
        self._handover = None
        if not preview:
            self._suspended = None
        self._resume_on_fade = True
        # cancel any running fade; the new playback owns the volume now
        self._fade_timer.stop()
        self._fade_done = None
        if changed:
            self.player.stop()
            self.track = track
            applog.log(f"audio play: {track.id} preview={preview} path={track.abs_path}")
            self.player.setSource(QUrl.fromLocalFile(str(track.abs_path)))
            self._load_spectrum(track)
        self.previewing = preview
        self.full_mode = not preview
        self._preview_pending = False
        self._preview_stop = start_ms + preview_len if preview else 0
        if preview:
            # audition fades are longer for longer auditions
            self._preview_fade_in, self._preview_fade_out = self._preview_fade_times(preview_len)
            self._fade = 0.0
            self._apply_volume()
            self._start_fade(1.0, self._preview_fade_in)
        else:
            # A resumed concert fades back in instead of jumping to the
            # previous volume.
            self._fade = 0.0 if fade_in > 0 else 1.0
            self._apply_volume()
        self.player.play()
        if not preview and fade_in > 0:
            self._start_fade(1.0, fade_in)
        if not changed and preview:
            self.player.setPosition(int(start_ms))
        if start_ms > 0:
            # seek after the backend has opened the file
            QTimer.singleShot(80, lambda: self.player.setPosition(int(start_ms)))
        self._timer.start()
        self.stateChanged.emit()

    def stop(self) -> None:
        self.stop_title_bgm()
        self._handover = None
        self._fade_timer.stop()
        self._fade_done = None
        self._fade = 1.0
        self._apply_volume()
        self.player.stop()
        self.previewing = False
        self.full_mode = False
        self._preview_pending = False
        self._preview_stop = 0
        self._suspended = None
        self._resume_on_fade = True
        self._timer.stop()
        self.stateChanged.emit()

    def stop_preview(self, fade: bool = True, resume: bool = True) -> None:
        """Stop an audition.  Default is a soft fade-out, not a hard cut."""
        if self._handover is not None:
            # The audition has not started yet; fade the parked concert
            # back in and drop the request.
            self.cancel_handover(restore=True)
            self.stateChanged.emit()
            return
        if not self.previewing and not self._preview_pending:
            return
        self._timer.stop()
        self._preview_stop = 0
        self.previewing = False
        self._resume_on_fade = resume
        if fade:
            self._preview_pending = True
            self._start_fade(0.0, self._preview_fade_out, self._finish_preview_fade)
        else:
            self._fade = 0.0
            self._apply_volume()
            self.player.pause()
            self._emit_preview_end()
        self.stateChanged.emit()

    def set_preview_remaining(self, ms: int) -> None:
        if self.previewing:
            self._preview_stop = self.player.position() + max(0, int(ms))

    def _finish_preview_fade(self) -> None:
        self.player.pause()
        self._emit_preview_end()
        self.stateChanged.emit()

    # -- volume fades -----------------------------------------------------
    def _apply_volume(self) -> None:
        self.out.setVolume(max(0.0, min(1.0, self._base_volume * self._fade)))

    def _start_fade(self, target: float, duration: float, done=None) -> None:
        self._fade_from = self._fade
        self._fade_to = max(0.0, min(1.0, target))
        self._fade_t0 = time.monotonic()
        self._fade_dur = max(0.02, duration)
        self._fade_done = done
        self._fade_timer.start()

    def _fade_step(self) -> None:
        k = min(1.0, (time.monotonic() - self._fade_t0) / self._fade_dur)
        e = k * k * (3 - 2 * k)
        self._fade = self._fade_from + (self._fade_to - self._fade_from) * e
        self._apply_volume()
        if k >= 1.0:
            self._fade_timer.stop()
            done = self._fade_done
            self._fade_done = None
            if done is not None:
                done()

    def pause(self) -> None:
        self.player.pause()
        self.stateChanged.emit()

    def toggle(self) -> None:
        if self.player.playbackState() == QMediaPlayer.PlaybackState.PlayingState:
            self.player.pause()
        else:
            if self._fade < 0.99 and not self.previewing:
                # never resume into a stale fade-out
                self._fade = 1.0
                self._apply_volume()
            self.player.play()
        self.stateChanged.emit()

    def seek(self, ms: int) -> None:
        ms = max(0, int(ms))
        self.player.setPosition(ms)
        self.positionChanged.emit(ms)

    def set_volume(self, v: float) -> None:
        self._base_volume = max(0.0, min(1.0, float(v)))
        self._apply_volume()

    def volume(self) -> float:
        return self._base_volume

    def set_muted(self, muted: bool) -> None:
        self.out.setMuted(bool(muted))

    def output_devices(self):
        return QMediaDevices.audioOutputs()

    def set_output_device(self, index: int) -> None:
        devs = self.output_devices()
        if 0 <= index < len(devs):
            self.out.setDevice(devs[index])

    # ------------------------------------------------------------------
    def _load_spectrum(self, track) -> None:
        if self._spec_id == track.id:
            return
        self._spec_id = track.id
        p = paths.data("cache", "spectrum", f"{track.id}.npz")
        try:
            z = np.load(p)
            self._spec = (z["spec"].astype(np.float32), z["bass"].astype(np.float32) / 255.0,
                          z["energy"].astype(np.float32) / 255.0, int(z["fps"]))
        except Exception:
            self._spec = None

    def spectrum_at(self, ms: int) -> tuple[np.ndarray, float, float]:
        if self._spec is None:
            t = time.monotonic()
            base = 0.28 + 0.18 * math.sin(t * 1.7)
            bands = np.array([max(0.0, min(1.0, base + 0.22 * math.sin(t * 2.3 + i * 0.38) + 0.12 * math.sin(t * 5.1 + i * 0.91)))
                              for i in range(48)], dtype=np.float32)
            return bands, 0.35 + 0.12 * math.sin(t * 2.1), 0.45
        spec, bass, energy, fps = self._spec
        f = max(0.0, ms / 1000.0 * fps)
        i = min(int(f), len(spec) - 1)
        j = min(i + 1, len(spec) - 1)
        a = f - i
        bands = (spec[i] * (1 - a) + spec[j] * a) / 255.0
        b = float(bass[i] * (1 - a) + bass[j] * a)
        e = float(energy[i] * (1 - a) + energy[j] * a)
        return bands, b, e
