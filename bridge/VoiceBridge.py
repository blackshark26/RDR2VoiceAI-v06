#!/usr/bin/env python3
"""
RDR2VoiceAI VoiceBridge v0.6 BARGE-IN MEMORY

Main behavior:
- Hold U to speak.
- If the NPC is currently speaking and U is pressed:
    * NPC audio playback stops immediately.
    * Gemini Live session is NOT closed.
    * Existing conversation context remains in the same session.
    * Player microphone starts immediately.
    * New speech interrupts the model turn naturally.
- When U is released, the NPC answers the new player turn using the same
  conversation memory.

Also keeps v0.5 features:
- continuous microphone input stream
- male/female voice selection from actual NPC gender
- modular Prompts / Languages / Personalities / World / Actions
- NPC engagement bridge to the ASI
"""

from __future__ import annotations

import asyncio
import json
import logging
import queue
import sys
import time
import traceback
from pathlib import Path
from typing import Dict, Optional

import sounddevice as sd
from google import genai
from google.genai import types


# ---------------------------------------------------------------------------
# Paths / audio
# ---------------------------------------------------------------------------
if getattr(sys, "frozen", False):
    ROOT = Path(sys.executable).resolve().parent
else:
    ROOT = Path(__file__).resolve().parent

CFG_PATH = ROOT / "config.json"
LOG_PATH = ROOT / "VoiceBridge.log"
EVENTS_PATH = ROOT / "RDR2VoiceAI_events.txt"
COMMANDS_PATH = ROOT / "RDR2VoiceAI_commands.txt"

IN_RATE = 16000
OUT_RATE = 24000
CHANNELS = 1
BLOCKSIZE = 1280  # 80ms at 16kHz, one persistent PortAudio stream while PTT is held.

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[
        logging.FileHandler(LOG_PATH, encoding="utf-8"),
        logging.StreamHandler(sys.stdout),
    ],
)
log = logging.getLogger("VoiceBridge")


# ---------------------------------------------------------------------------
# Utility
# ---------------------------------------------------------------------------
def load_json(path: Path, default):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception as e:
        log.warning("Could not load %s: %s", path, e)
        return default


def load_text(path: Path, default=""):
    try:
        return path.read_text(encoding="utf-8").strip()
    except Exception as e:
        log.warning("Could not load %s: %s", path, e)
        return default


def load_config():
    if not CFG_PATH.exists():
        raise SystemExit("Missing config.json next to RDR2VoiceBridge.exe")

    cfg = json.loads(CFG_PATH.read_text(encoding="utf-8"))
    key = str(cfg.get("api_key", "")).strip()

    if not key or "PUT_YOUR" in key:
        raise SystemExit("Set your Gemini API key in config.json")

    return cfg


def parse_context(ctx: str) -> Dict[str, str]:
    result = {}
    for item in (ctx or "").split(";"):
        if "=" in item:
            key, value = item.split("=", 1)
            result[key.strip().lower()] = value.strip()
    return result


def append_command(name: str, npc: int):
    try:
        with COMMANDS_PATH.open("a", encoding="utf-8") as f:
            f.write(f"{name}|{npc}\n")
            f.flush()
    except Exception as e:
        log.warning("Command write failed: %s", e)


def read_new_events(path: Path, offset: int):
    if not path.exists():
        return offset, []

    try:
        with path.open("r", encoding="utf-8", errors="ignore") as f:
            f.seek(offset)
            lines = f.readlines()
            return f.tell(), [line.strip() for line in lines if line.strip()]
    except Exception as e:
        log.warning("Event read error: %s", e)
        return offset, []


# ---------------------------------------------------------------------------
# Modular data
# ---------------------------------------------------------------------------
class DataPack:
    def __init__(self, cfg):
        self.cfg = cfg
        paths = cfg.get("paths", {})

        self.prompts = ROOT / paths.get("prompts", "Prompts")
        self.languages = ROOT / paths.get("languages", "Languages")
        self.personalities = ROOT / paths.get("personalities", "Personalities")
        self.world = ROOT / paths.get("world", "World")
        self.actions = ROOT / paths.get("actions", "Actions")

        self.signature = None

    def sig(self):
        result = []
        roots = (
            self.prompts,
            self.languages,
            self.personalities,
            self.world,
            self.actions,
        )

        for root in roots:
            if root.exists():
                for path in sorted(root.rglob("*")):
                    if path.is_file():
                        try:
                            stat = path.stat()
                            result.append(
                                (str(path), stat.st_mtime_ns, stat.st_size)
                            )
                        except OSError:
                            pass

        return tuple(result)

    def changed(self):
        current = self.sig()

        if self.signature is None:
            self.signature = current
            return False

        if current != self.signature:
            self.signature = current
            return True

        return False

    def language_data(self):
        code = self.cfg.get("language_profile", "ar-MA")
        folder = self.languages / code

        return (
            code,
            load_json(folder / "language.json", {}),
            load_json(folder / "vocabulary.json", {"words": []}),
            load_json(
                folder / "recognition.json",
                {"language_codes": ["ar"], "mode": "SMART"},
            ),
        )

    def role_file(self, role):
        role = (role or "").lower()

        if "sheriff" in role or "law" in role:
            return "Sheriff.txt"
        if "bartender" in role or "saloon" in role:
            return "Bartender.txt"
        if "farmer" in role or "ranch" in role:
            return "Farmer.txt"
        if "merchant" in role or "shop" in role or "store" in role:
            return "Merchant.txt"
        if "outlaw" in role or "bandit" in role:
            return "Outlaw.txt"
        if "hunter" in role or "trapper" in role:
            return "Hunter.txt"
        if "doctor" in role:
            return "Doctor.txt"

        return "Civilian.txt"

    def prompt(self, npc_ctx):
        context = parse_context(npc_ctx)

        role = context.get(
            "role",
            self.cfg.get("default_role", "Civilian"),
        )

        if role == "civilian":
            role = self.cfg.get("default_role", "Civilian")

        base = load_text(self.prompts / "Base.txt")
        role_text = load_text(self.prompts / self.role_file(role))

        personality_name = self.cfg.get(
            "personality_profile",
            "friendly",
        )
        personality = load_json(
            self.personalities / f"{personality_name}.json",
            {},
        )

        lore = load_text(self.world / "lore_1899.txt")
        actions = load_text(self.actions / "rules.txt")

        _, language, _, _ = self.language_data()
        language_rules = "\n".join(
            "- " + rule
            for rule in language.get("rules", [])
        )

        gender = context.get("gender", "unknown")
        max_sentences = int(
            self.cfg.get("max_reply_sentences", 2)
        )

        return f"""{base}

{role_text}

PERSONALITY:
{personality.get('prompt', '')}

NPC SEX / VOICE CONTEXT:
- The visible NPC is {gender}.
- The spoken voice is selected externally to match the visible NPC.
- Do not claim to be a different sex from the visible character.

MOROCCAN DARIJA UNDERSTANDING:
- Treat spoken Moroccan Darija as valid Arabic even when pronunciation and grammar are colloquial.
- Expect vocabulary such as: بغيت، شنو، فين، علاش، دابا، واش، راه، حيت، بزاف، شوية، هضر، صافي، واخا.
- Moroccan Darija may naturally contain occasional French words.
- Never generate an unrelated answer merely because one word was uncertain.
- Infer the meaning from the whole sentence and conversation context.
- Ask the player to repeat only if the actual request cannot reasonably be inferred.

CONVERSATION / INTERRUPTION RULE:
- The player may interrupt you while you are speaking.
- If interrupted, do NOT forget the conversation.
- Treat the player's interruption as a natural continuation of the SAME conversation.
- Remember subjects, names, claims and facts established in earlier turns.
- Do not restart the conversation or greet the player again after an interruption.
- Respond directly to what the player just said, using the previous discussion as context.

LANGUAGE PROFILE:
{language_rules}

RESPONSE:
- Maximum {max_sentences} short sentences unless more detail is explicitly requested.
- Answer the player's actual question.
- Do not repeat yourself unless clarification is needed.

{lore}

{actions}

CURRENT NPC CONTEXT:
{npc_ctx}
""".strip()

    def transcription(self):
        _, _, vocabulary, recognition = self.language_data()

        return (
            recognition.get("language_codes", ["ar"]),
            vocabulary.get("words", []),
            recognition.get("mode", "SMART"),
        )


# ---------------------------------------------------------------------------
# Continuous microphone
# ---------------------------------------------------------------------------
class Mic:
    def __init__(self, device=None):
        self.device = device
        self.q = queue.Queue(maxsize=1024)
        self.stream = None

    def callback(self, indata, frames, time_info, status):
        if status:
            log.debug("Mic status: %s", status)

        try:
            self.q.put_nowait(bytes(indata))
        except queue.Full:
            try:
                self.q.get_nowait()
            except queue.Empty:
                pass

            try:
                self.q.put_nowait(bytes(indata))
            except queue.Full:
                pass

    def start(self):
        self.stop()
        self.clear()

        self.stream = sd.RawInputStream(
            samplerate=IN_RATE,
            blocksize=BLOCKSIZE,
            channels=CHANNELS,
            dtype="int16",
            device=self.device,
            callback=self.callback,
        )
        self.stream.start()

    def stop(self):
        if self.stream is not None:
            try:
                self.stream.stop()
            except Exception:
                pass

            try:
                self.stream.close()
            except Exception:
                pass

            self.stream = None

    def clear(self):
        while True:
            try:
                self.q.get_nowait()
            except queue.Empty:
                break

    def drain(self):
        chunks = []

        while True:
            try:
                chunks.append(self.q.get_nowait())
            except queue.Empty:
                break

        return chunks


# ---------------------------------------------------------------------------
# Gemini Live session
# ---------------------------------------------------------------------------
class LiveBridge:
    def __init__(self, cfg, data):
        self.cfg = cfg
        self.data = data

        self.client = genai.Client(
            api_key=cfg["api_key"]
        )

        self.model = cfg.get(
            "model",
            "gemini-3.8-live",
        )

        self.session = None
        self.session_cm = None
        self.current_key = None
        self.turn_active = False

        # Barge-in / playback state.
        self.playback_interrupted = False
        self.output_stream = None

    def voice_for(self, npc_ctx):
        gender = parse_context(npc_ctx).get(
            "gender",
            "unknown",
        )

        if gender == "female":
            return (
                self.cfg.get("female_voice", "Kore"),
                gender,
            )

        return (
            self.cfg.get("male_voice", "Orus"),
            gender,
        )

    async def close(self):
        self.turn_active = False

        self.interrupt_local_playback()

        if self.session_cm is not None:
            try:
                await self.session_cm.__aexit__(
                    None,
                    None,
                    None,
                )
            except Exception as e:
                log.debug("Session close: %s", e)

        self.session = None
        self.session_cm = None
        self.current_key = None

    async def connect(self, npc_ctx="", force=False):
        voice, gender = self.voice_for(npc_ctx)
        prompt = self.data.prompt(npc_ctx)

        # Session identity. Same NPC role/gender/prompt = reuse session and memory.
        session_key = (voice, prompt)

        if (
            self.cfg.get("auto_reload_data", True)
            and self.data.changed()
        ):
            force = True

        if (
            self.session is not None
            and session_key != self.current_key
        ):
            force = True

        if self.session is not None and not force:
            return self.session

        if force:
            await self.close()

        language_codes, vocabulary, mode = (
            self.data.transcription()
        )

        transcription = types.AudioTranscriptionConfig(
            language_codes=language_codes,
            custom_vocabulary=vocabulary,
            mode=mode,
        )

        live_cfg = types.LiveConnectConfig(
            response_modalities=["AUDIO"],
            system_instruction=types.Content(
                parts=[
                    types.Part(text=prompt)
                ]
            ),
            speech_config=types.SpeechConfig(
                voice_config=types.VoiceConfig(
                    prebuilt_voice_config=
                    types.PrebuiltVoiceConfig(
                        voice_name=voice
                    )
                )
            ),
            input_audio_transcription=transcription,
            output_audio_transcription={},
            realtime_input_config=
            types.RealtimeInputConfig(
                automatic_activity_detection=
                types.AutomaticActivityDetection(
                    disabled=True
                )
            ),
        )

        log.info(
            "Connecting Gemini | gender=%s voice=%s",
            gender,
            voice,
        )

        self.session_cm = self.client.aio.live.connect(
            model=self.model,
            config=live_cfg,
        )

        self.session = await self.session_cm.__aenter__()
        self.current_key = session_key

        log.info("Gemini Live session ready")
        return self.session

    def interrupt_local_playback(self):
        """
        IMPORTANT:
        This stops ONLY local NPC audio playback.
        It intentionally does NOT close Gemini session.
        """
        self.playback_interrupted = True

        stream = self.output_stream
        if stream is not None:
            try:
                # Abort pending speaker audio as quickly as PortAudio allows.
                stream.abort(ignore_errors=True)
            except TypeError:
                try:
                    stream.abort()
                except Exception:
                    pass
            except Exception:
                pass

        log.info(
            "BARGE-IN: local NPC playback interrupted; Gemini session preserved"
        )

    async def begin_player_turn(self, npc_ctx):
        """
        Start a player turn in the SAME Live session.
        Sending activity_start while the model is responding is the
        barge-in signal; we do not reconnect, so prior conversation context remains.
        """
        session = await self.connect(npc_ctx)

        self.playback_interrupted = True

        try:
            await session.send_realtime_input(
                activity_start=types.ActivityStart()
            )
        except Exception:
            # Only reconnect if the socket is actually dead.
            # This is NOT the normal barge-in path.
            log.warning(
                "Live socket unavailable; reconnecting because connection failed."
            )
            session = await self.connect(
                npc_ctx,
                force=True,
            )
            await session.send_realtime_input(
                activity_start=types.ActivityStart()
            )

        self.turn_active = True

    async def send_audio(self, raw):
        if (
            self.turn_active
            and self.session is not None
            and raw
        ):
            await self.session.send_realtime_input(
                audio=types.Blob(
                    data=raw,
                    mime_type="audio/pcm;rate=16000",
                )
            )

    async def end_player_input(self):
        """
        Ends microphone turn immediately.
        Does not receive/play here, so the main event loop remains free
        to detect another U press during the NPC answer.
        """
        if (
            not self.turn_active
            or self.session is None
        ):
            return

        self.turn_active = False

        await self.session.send_realtime_input(
            activity_end=types.ActivityEnd()
        )

    async def cancel_player_turn(self):
        if (
            self.turn_active
            and self.session is not None
        ):
            try:
                await self.session.send_realtime_input(
                    activity_end=types.ActivityEnd()
                )
            except Exception:
                pass

        self.turn_active = False

    async def receive_reply(self, npc):
        """
        Receives exactly one model turn.

        If U is pressed while this coroutine is running:
        - main loop calls interrupt_local_playback()
        - audio output is aborted / muted
        - this coroutine keeps consuming the old model turn until Gemini
          marks it interrupted or complete
        - session itself remains open and retains conversation history
        """
        if self.session is None:
            return

        self.playback_interrupted = False
        transcript = []
        first_audio = False
        last_pulse = time.monotonic()

        append_command(
            "NPC_SPEAK_START",
            npc,
        )

        output = None

        try:
            output = sd.RawOutputStream(
                samplerate=OUT_RATE,
                channels=1,
                dtype="int16",
            )
            output.start()
            self.output_stream = output

            async for response in self.session.receive():
                server = getattr(
                    response,
                    "server_content",
                    None,
                )

                if not server:
                    continue

                # Gemini can explicitly tell us that this model turn was interrupted.
                if getattr(server, "interrupted", False):
                    log.info(
                        "Gemini confirmed model turn interruption"
                    )
                    break

                now = time.monotonic()
                if now - last_pulse > 1.6:
                    append_command(
                        "NPC_SPEAK_PULSE",
                        npc,
                    )
                    last_pulse = now

                model_turn = getattr(
                    server,
                    "model_turn",
                    None,
                )

                if (
                    model_turn
                    and getattr(model_turn, "parts", None)
                ):
                    for part in model_turn.parts:
                        inline = getattr(
                            part,
                            "inline_data",
                            None,
                        )

                        if inline and inline.data:
                            if not self.playback_interrupted:
                                if not first_audio:
                                    first_audio = True
                                    print(
                                        "[*] NPC replying..."
                                    )
                                    log.info(
                                        "First reply audio chunk"
                                    )

                                try:
                                    # Keep event loop responsive to U while
                                    # PortAudio writes the current small chunk.
                                    await asyncio.to_thread(
                                        output.write,
                                        inline.data,
                                    )
                                except Exception as e:
                                    if self.playback_interrupted:
                                        log.debug(
                                            "Output aborted by barge-in: %s",
                                            e,
                                        )
                                    else:
                                        log.warning(
                                            "Audio playback error: %s",
                                            e,
                                        )

                            # If interrupted, audio bytes are deliberately
                            # discarded while server turn is drained.

                        text = getattr(
                            part,
                            "text",
                            None,
                        )

                        if text:
                            transcript.append(text)

                output_tx = getattr(
                    server,
                    "output_transcription",
                    None,
                )

                if (
                    output_tx
                    and getattr(output_tx, "text", None)
                ):
                    transcript.append(
                        output_tx.text
                    )

                if getattr(
                    server,
                    "turn_complete",
                    False,
                ):
                    break

        finally:
            self.output_stream = None

            if output is not None:
                try:
                    output.abort(ignore_errors=True)
                except TypeError:
                    try:
                        output.abort()
                    except Exception:
                        pass
                except Exception:
                    pass

                try:
                    output.close(ignore_errors=True)
                except TypeError:
                    try:
                        output.close()
                    except Exception:
                        pass
                except Exception:
                    pass

            # Keep NPC engaged; ASI will continue looking at player.
            append_command(
                "NPC_SPEAK_PULSE",
                npc,
            )

        if transcript:
            log.info(
                "NPC transcript: %s",
                " ".join(transcript).strip()[:500],
            )


# ---------------------------------------------------------------------------
# Reply sequencing
# ---------------------------------------------------------------------------
async def receive_after_previous(
    bridge: LiveBridge,
    previous_task: Optional[asyncio.Task],
    npc: int,
):
    """
    Never run two session.receive() iterators at the same time.

    After barge-in, the previous reply task drains until Gemini marks it
    interrupted. The new reply then starts on the SAME session.
    """
    if previous_task is not None:
        try:
            await previous_task
        except asyncio.CancelledError:
            pass
        except Exception as e:
            log.warning(
                "Previous reply ended with error: %s",
                e,
            )

    await bridge.receive_reply(npc)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
async def main():
    cfg = load_config()

    data = DataPack(cfg)
    bridge = LiveBridge(cfg, data)
    mic = Mic(
        cfg.get("input_device", None)
    )

    offset = (
        EVENTS_PATH.stat().st_size
        if EVENTS_PATH.exists()
        else 0
    )

    recording = False
    ready_to_send = False

    npc = 0
    ctx = ""
    bytes_sent = 0

    # Current/last reply task.
    reply_task: Optional[asyncio.Task] = None

    print("=" * 72)
    print(
        "RDR2 Voice AI Bridge v0.6 BARGE-IN MEMORY"
    )
    print(
        "Press/Hold U at any time - even while the NPC is talking."
    )
    print(
        "NPC audio stops immediately; conversation memory stays in the same session."
    )
    print(
        f"Language: {cfg.get('language_profile', 'ar-MA')}"
    )
    print("=" * 72)

    log.info(
        "v0.6 Barge-In Memory started"
    )

    try:
        while True:
            offset, events = read_new_events(
                EVENTS_PATH,
                offset,
            )

            for event in events:
                parts = event.split("|")
                name = parts[0]

                event_npc = (
                    int(parts[1])
                    if len(parts) > 1
                    and parts[1].isdigit()
                    else 0
                )

                extra = (
                    "|".join(parts[2:])
                    if len(parts) > 2
                    else ""
                )

                if name == "READY":
                    log.info("ASI READY")

                elif name == "NO_NPC":
                    print(
                        "[!] No human NPC within range."
                    )

                elif name == "PTT_START":
                    if recording:
                        continue

                    npc = event_npc
                    ctx = extra
                    bytes_sent = 0
                    ready_to_send = False

                    # ---------- BARGE-IN ----------
                    if (
                        cfg.get(
                            "barge_in_enabled",
                            True,
                        )
                        and reply_task is not None
                        and not reply_task.done()
                    ):
                        print(
                            "[*] BARGE-IN: NPC voice stopped. Listening..."
                        )
                        log.info(
                            "Player interrupted NPC reply with U"
                        )

                        # Stop ONLY speaker playback.
                        # Same Gemini session and context are preserved.
                        bridge.interrupt_local_playback()

                    # Start mic BEFORE any network work, so first words aren't lost.
                    mic.start()
                    recording = True

                    print(
                        f"[*] Listening... NPC={npc} {ctx}"
                    )

                    try:
                        # If a model reply is still being drained, activity_start
                        # tells the SAME Live session that the player interrupted.
                        await bridge.begin_player_turn(ctx)
                        ready_to_send = True

                        # Send anything captured during tiny connection/activity delay.
                        for chunk in mic.drain():
                            await bridge.send_audio(
                                chunk
                            )
                            bytes_sent += len(chunk)

                    except Exception as e:
                        recording = False
                        mic.stop()

                        log.error(
                            "Begin failed: %s\n%s",
                            e,
                            traceback.format_exc(),
                        )

                        print(
                            f"[!] Gemini error: {e}"
                        )

                        await bridge.close()

                elif name == "PTT_STOP":
                    if not recording:
                        continue

                    recording = False
                    mic.stop()

                    if ready_to_send:
                        for chunk in mic.drain():
                            await bridge.send_audio(
                                chunk
                            )
                            bytes_sent += len(chunk)

                    log.info(
                        "PTT_STOP bytes=%d",
                        bytes_sent,
                    )

                    if (
                        bytes_sent
                        < int(IN_RATE * 2 * 0.25)
                    ):
                        print(
                            "[!] Speech too short."
                        )
                        await bridge.cancel_player_turn()
                        continue

                    print(
                        "[*] Waiting for NPC reply..."
                    )

                    try:
                        # End player turn NOW.
                        await bridge.end_player_input()

                        # Capture old reply task if this turn began by interrupting it.
                        previous = (
                            reply_task
                            if reply_task is not None
                            and not reply_task.done()
                            else None
                        )

                        # Do NOT await this here.
                        # Main loop stays alive so another U can interrupt again.
                        reply_task = asyncio.create_task(
                            receive_after_previous(
                                bridge,
                                previous,
                                npc,
                            )
                        )

                    except Exception as e:
                        log.error(
                            "End turn failed: %s\n%s",
                            e,
                            traceback.format_exc(),
                        )

                        print(
                            f"[!] Gemini error: {e}"
                        )

                        await bridge.close()

                elif name in (
                    "CANCEL",
                    "TIMEOUT",
                ):
                    recording = False
                    mic.stop()

                    bridge.interrupt_local_playback()

                    await bridge.cancel_player_turn()

                    if npc:
                        append_command(
                            "NPC_RELEASE",
                            npc,
                        )

                    print(
                        f"[*] {name}"
                    )

            # Stream microphone while U is held.
            if recording and ready_to_send:
                chunks = mic.drain()

                for chunk in chunks:
                    try:
                        await bridge.send_audio(
                            chunk
                        )
                        bytes_sent += len(chunk)

                    except Exception as e:
                        log.error(
                            "Audio send failed: %s",
                            e,
                        )

                        recording = False
                        mic.stop()
                        await bridge.close()
                        break

            # Log asynchronous reply failures without killing main loop.
            if (
                reply_task is not None
                and reply_task.done()
            ):
                try:
                    exc = reply_task.exception()
                except asyncio.CancelledError:
                    exc = None

                if exc is not None:
                    log.error(
                        "Reply task failed: %s\n%s",
                        exc,
                        "".join(
                            traceback.format_exception(
                                type(exc),
                                exc,
                                exc.__traceback__,
                            )
                        ),
                    )

                    print(
                        f"[!] Reply error: {exc}"
                    )

                # Leave completed task reference until overwritten.
                # Its conversation remains inside the Gemini session.

            await asyncio.sleep(0.01)

    except KeyboardInterrupt:
        pass

    finally:
        mic.stop()

        bridge.interrupt_local_playback()

        if reply_task is not None:
            try:
                await asyncio.wait_for(
                    asyncio.shield(reply_task),
                    timeout=1.0,
                )
            except Exception:
                pass

        if npc:
            append_command(
                "NPC_RELEASE",
                npc,
            )

        await bridge.close()

        log.info(
            "v0.6 Barge-In Memory stopped"
        )


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except Exception as e:
        log.error(
            "Fatal: %s\n%s",
            e,
            traceback.format_exc(),
        )
        print(
            f"Fatal error: {e}"
        )
        sys.exit(1)
