"""
Conversational LLM tone engine.

The user can ask for a tone, name a song/artist, or iterate on the current sound.
The model replies conversationally and changes the sound only when it calls the
`set_amp_params` tool. OpenAI is the default provider when OPENAI_API_KEY is set;
Anthropic remains available via SF4_LLM_PROVIDER=anthropic.
"""

from __future__ import annotations

import json
import os
from typing import Callable

from .models import AmpParams, ChatMessage, Telemetry
from .openai_client import make_openai_client

DEFAULT_OPENAI_MODEL = "gpt-5.5"
DEFAULT_ANTHROPIC_MODEL = "claude-sonnet-4-6"
MAX_TOKENS = 2048
MAX_TOOL_ROUNDS = 4

SYSTEM_PROMPT = """\
You are the tone engine for the SF4 smart guitar amp - a real-time Arduino DSP pedal. You translate what a guitarist asks for into the amp's parameters and chat with them about their sound.

The amp runs exactly ONE effect at a time. Effects and their parameters:

- clean       : transparent passthrough. No effect parameters.
- overdrive   : hard-clipping distortion. `drive` is the gain (1-511): ~16 = edge
                of breakup, ~180 = rock crunch, ~480 = saturated metal.
- delay       : single echo with feedback. `delay_len` (1-512 samples) sets the
                echo time (t = delay_len / 9615 Hz, up to ~53 ms - slapback range).
                `feedback` (0-255) sets how many repeats; `mix` (0-255) how loud.
- chorus      : modulated short delay, 50/50 dry/wet. `depth` (1-48) sets the
                sweep amount; `rate` (1-20) the LFO speed.
- reverb      : dense feedback network. `feedback` (0-255) sets the tail length -
                KEEP IT UNDER 205 or it self-oscillates. `mix` (0-255) the wet level.
- tuner       : guitar tuner. Clean passthrough while the amp detects the played
                string's pitch and the UI shows it against standard tuning (EADGBE).
                No effect parameters. Switch here when the user wants to tune up.

`auto_vga` (default true) keeps the input gain managed automatically; only set a
manual `gain` (0-255) if the user explicitly asks about input level.

Guidelines:
- Tone-setting requests are action requests, not questions. If the user asks for
  a sound, tone, artist/song style, more/less of an effect, or says make/set/give/switch,
  you MUST call `set_amp_params` before replying. Never answer only in text to a
  tone-setting request.
- Pick the single best effect and musically sensible values. Use your own knowledge
  of artists' and songs' signature tones.
- Only change the fields that matter; the rest keep their current values.
- For iterative requests ("a touch more space", "less drive"), adjust relative to
  the CURRENT settings you are given.
- If the user just asks a factual question or chats without requesting a sound change,
  answer WITHOUT calling the tool.
- Keep replies short and friendly - a sentence or two. Briefly say what you set."""


def _tool_schema() -> dict:
    """JSON schema for set_amp_params - all fields optional (partial update)."""
    return {
        "name": "set_amp_params",
        "description": (
            "Set the amp's sound. Provide only the fields you want to change; "
            "omitted fields keep their current value. Call this whenever the user "
            "wants the sound changed."
        ),
        "input_schema": {
            "type": "object",
            "properties": {
                "effect": {
                    "type": "string",
                    "enum": ["clean", "overdrive", "delay", "chorus", "reverb",
                             "tuner"],
                    "description": "Which effect/mode to activate.",
                },
                "drive": {"type": "integer", "minimum": 1, "maximum": 511,
                          "description": "Overdrive gain."},
                "delay_len": {"type": "integer", "minimum": 1, "maximum": 512,
                              "description": "Delay time in samples."},
                "feedback": {"type": "integer", "minimum": 0, "maximum": 255,
                             "description": "Delay/reverb tail (<205 for reverb)."},
                "mix": {"type": "integer", "minimum": 0, "maximum": 255,
                        "description": "Wet level for delay/reverb."},
                "depth": {"type": "integer", "minimum": 1, "maximum": 48,
                          "description": "Chorus sweep depth."},
                "rate": {"type": "integer", "minimum": 1, "maximum": 20,
                         "description": "Chorus LFO rate."},
                "auto_vga": {"type": "boolean",
                             "description": "Automatic input gain on/off."},
                "gain": {"type": "integer", "minimum": 0, "maximum": 255,
                         "description": "Manual input gain (forces auto_vga off)."},
            },
            "additionalProperties": False,
        },
    }


def _openai_tool_schema(tool: dict) -> dict:
    return {
        "type": "function",
        "function": {
            "name": tool["name"],
            "description": tool["description"],
            "parameters": tool["input_schema"],
        },
    }


def _context_line(params: AmpParams, tel: Telemetry | None) -> str:
    cur = params.model_dump()
    cur["effect"] = params.effect.name.lower()
    line = f"[current settings: {json.dumps(cur)}]"
    if tel is not None:
        line += f" [telemetry: peak={tel.peak} clip={tel.clip} vga={tel.vga}]"
    return line


def _provider_from_env() -> str:
    explicit = os.environ.get("SF4_LLM_PROVIDER", "").strip().lower()
    if explicit in {"openai", "anthropic"}:
        return explicit
    if os.environ.get("OPENAI_API_KEY"):
        return "openai"
    if os.environ.get("ANTHROPIC_API_KEY") or os.environ.get("ANTHROPIC_AUTH_TOKEN"):
        return "anthropic"
    raise RuntimeError("OPENAI_API_KEY or ANTHROPIC_API_KEY is not set")


_TONE_ACTION_WORDS = {
    "make", "set", "give", "switch", "change", "turn", "add", "remove",
    "more", "less", "increase", "decrease", "boost", "reduce", "dial",
    "tone", "sound", "effect", "overdrive", "drive", "distortion", "clean",
    "delay", "echo", "chorus", "reverb", "ambient", "blues", "metal",
    "crunch", "lead", "solo", "rhythm", "gilmour", "hendrix", "slapback",
}


def _should_force_tool(message: str) -> bool:
    text = message.lower()
    return any(word in text for word in _TONE_ACTION_WORDS)


class ToneEngine:
    def __init__(self, model: str | None = None):
        self._provider = _provider_from_env()
        self._tool = _tool_schema()

        if self._provider == "openai":
            self._client = make_openai_client()
            chosen = model or os.environ.get("OPENAI_CHAT_MODEL") or os.environ.get("SF4_MODEL")
            self._model = chosen if chosen and not chosen.startswith("claude-") else DEFAULT_OPENAI_MODEL
            self._openai_tool = _openai_tool_schema(self._tool)
            return

        import anthropic

        if not (os.environ.get("ANTHROPIC_API_KEY") or os.environ.get("ANTHROPIC_AUTH_TOKEN")):
            raise RuntimeError("ANTHROPIC_API_KEY is not set")

        self._client = anthropic.Anthropic()
        self._model = model or os.environ.get("SF4_MODEL", DEFAULT_ANTHROPIC_MODEL)
        self._thinking = (
            {"type": "disabled"} if "haiku" in self._model.lower()
            else {"type": "adaptive"}
        )

    def chat(
        self,
        message: str,
        history: list[ChatMessage],
        current: AmpParams,
        apply_fn: Callable[[AmpParams], AmpParams],
        telemetry: Telemetry | None = None,
    ) -> tuple[str, AmpParams, bool]:
        """Run one chat turn. Returns (reply_text, params, changed)."""
        if self._provider == "openai":
            return self._chat_openai(message, history, current, apply_fn, telemetry)
        return self._chat_anthropic(message, history, current, apply_fn, telemetry)

    def _apply_tool_patch(
        self,
        patch: dict,
        params: AmpParams,
        apply_fn: Callable[[AmpParams], AmpParams],
    ) -> tuple[AmpParams, dict]:
        try:
            params = apply_fn(params.merge(patch))
            cur = params.model_dump()
            cur["effect"] = params.effect.name.lower()
            return params, {"ok": True, "applied": cur}
        except Exception as exc:
            return params, {"ok": False, "error": str(exc)}

    def _chat_openai(
        self,
        message: str,
        history: list[ChatMessage],
        current: AmpParams,
        apply_fn: Callable[[AmpParams], AmpParams],
        telemetry: Telemetry | None,
    ) -> tuple[str, AmpParams, bool]:
        messages: list[dict] = [{"role": "developer", "content": SYSTEM_PROMPT}]
        messages.extend({"role": m.role, "content": m.content} for m in history)
        user_text = f"{_context_line(current, telemetry)}\n\n{message}"
        messages.append({"role": "user", "content": user_text})

        params = current
        changed = False
        reply_parts: list[str] = []

        force_tool = _should_force_tool(message)

        for _ in range(MAX_TOOL_ROUNDS):
            tool_choice = (
                {"type": "function", "function": {"name": "set_amp_params"}}
                if force_tool and not changed else "auto"
            )
            resp = self._client.chat.completions.create(
                model=self._model,
                messages=messages,
                # max_tokens=MAX_TOKENS,
                # temperature=0.2,
                tools=[self._openai_tool],
                tool_choice=tool_choice,
            )
            msg = resp.choices[0].message
            if msg.content:
                reply_parts.append(msg.content)

            tool_calls = list(msg.tool_calls or [])
            if not tool_calls:
                break

            messages.append({
                "role": "assistant",
                "content": msg.content or "",
                "tool_calls": [
                    {
                        "id": call.id,
                        "type": "function",
                        "function": {
                            "name": call.function.name,
                            "arguments": call.function.arguments or "{}",
                        },
                    }
                    for call in tool_calls
                ],
            })

            for call in tool_calls:
                if call.function.name != "set_amp_params":
                    continue
                try:
                    patch = json.loads(call.function.arguments or "{}")
                except json.JSONDecodeError as exc:
                    result = {"ok": False, "error": f"invalid tool JSON: {exc}"}
                else:
                    params, result = self._apply_tool_patch(patch, params, apply_fn)
                    changed = changed or bool(result.get("ok"))
                messages.append({
                    "role": "tool",
                    "tool_call_id": call.id,
                    "content": json.dumps(result),
                })

        reply = "\n".join(p.strip() for p in reply_parts if p.strip())
        if not reply:
            reply = "Done." if changed else "(no response)"
        return reply, params, changed

    def _chat_anthropic(
        self,
        message: str,
        history: list[ChatMessage],
        current: AmpParams,
        apply_fn: Callable[[AmpParams], AmpParams],
        telemetry: Telemetry | None,
    ) -> tuple[str, AmpParams, bool]:
        messages: list[dict] = [
            {"role": m.role, "content": m.content} for m in history
        ]
        user_text = f"{_context_line(current, telemetry)}\n\n{message}"
        messages.append({"role": "user", "content": user_text})

        params = current
        changed = False
        reply_parts: list[str] = []

        for _ in range(MAX_TOOL_ROUNDS):
            resp = self._client.messages.create(
                model=self._model,
                max_tokens=MAX_TOKENS,
                system=SYSTEM_PROMPT,
                thinking=self._thinking,
                tools=[self._tool],
                messages=messages,
            )

            for block in resp.content:
                if block.type == "text":
                    reply_parts.append(block.text)

            if resp.stop_reason != "tool_use":
                break

            messages.append({"role": "assistant", "content": resp.content})
            tool_results = []
            for block in resp.content:
                if block.type != "tool_use" or block.name != "set_amp_params":
                    continue
                params, result = self._apply_tool_patch(dict(block.input or {}), params, apply_fn)
                changed = changed or bool(result.get("ok"))
                tool_results.append({
                    "type": "tool_result",
                    "tool_use_id": block.id,
                    "content": json.dumps(result),
                })
            messages.append({"role": "user", "content": tool_results})

        reply = "\n".join(p.strip() for p in reply_parts if p.strip())
        if not reply:
            reply = "Done." if changed else "(no response)"
        return reply, params, changed
