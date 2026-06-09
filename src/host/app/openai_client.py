"""OpenAI client construction shared by chat and transcription."""

from __future__ import annotations

import os
from pathlib import Path

import httpx
from openai import OpenAI

_FALSE_VALUES = {"0", "false", "no", "off"}


def _verify_setting() -> str | bool:
    ca_bundle = os.environ.get("OPENAI_CA_BUNDLE", "").strip()
    if ca_bundle:
        path = Path(ca_bundle).expanduser()
        if not path.exists():
            raise RuntimeError(f"OPENAI_CA_BUNDLE does not exist: {path}")
        return str(path)

    verify = os.environ.get("OPENAI_VERIFY_SSL", "true").strip().lower()
    if verify in _FALSE_VALUES:
        return False
    return True


def make_openai_client(timeout: float = 60.0) -> OpenAI:
    api_key = os.environ.get("OPENAI_API_KEY")
    if not api_key:
        raise RuntimeError("OPENAI_API_KEY is not set")

    kwargs = {"api_key": api_key}
    base_url = os.environ.get("OPENAI_BASE_URL") or None
    if base_url:
        kwargs["base_url"] = base_url

    verify = _verify_setting()
    if verify is True:
        kwargs["timeout"] = timeout
    else:
        kwargs["http_client"] = httpx.Client(verify=verify, timeout=timeout)

    return OpenAI(**kwargs)
