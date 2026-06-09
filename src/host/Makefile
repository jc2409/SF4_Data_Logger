# SF4 host automation. Run from src/host/.
#   make help        show available commands
#   make env         create .env from the template
#   make install     install backend + frontend deps and build the UI
#   make run         install/build if needed, then start the web app
#   make dev         same as run, with uvicorn auto-reload
#   make web-build   build the React UI into app/static
#   make web-dev     run the Vite dev server (hot reload, proxies /api to :8000)
#   make port        print detected Arduino-like serial ports
#   make set-port    write the first detected Arduino-like port into .env
#   make serial-perms temporarily allow this shell user to open SF4_PORT
#   make doctor      print local dependency/configuration status
#   make cli ARGS="--demo overdrive"   bench-test the serial link
#   make flash       compile + upload the firmware (needs arduino-cli)

# Load .env so HOST/PORT/SF4_* are available to recipes (app also loads it itself).
-include .env
export

HOST ?= 127.0.0.1
PORT ?= 8000
FQBN ?= arduino:avr:uno
SKETCH ?= ../DSP_Pipeline
VENV := .venv
PYTHON ?= python3
PIP := $(VENV)/bin/pip
PY := $(VENV)/bin/python
UVICORN := $(VENV)/bin/uvicorn
SERIALCLI := $(PY) sf4_serial.py
NPM_INSTALL := npm --prefix web ci --include=dev --no-audit --no-fund --progress=false

# Prefer uv when explicitly installed; fall back to a plain local venv.
UV := $(shell command -v uv 2>/dev/null)
ifdef UV
  UVICORN = uv run uvicorn
  SERIALCLI = uv run python sf4_serial.py
endif

.DEFAULT_GOAL := help
.PHONY: help env install py-install web-install web-build web-dev run dev cli flash clean \
        port set-port serial-perms doctor

help:
	@grep -E '^#   make' Makefile | sed 's/^#   /  /'

env:
	@test -f .env || cp .env.example .env
	@echo ".env ready - set OPENAI_API_KEY, OPENAI_BASE_URL/CA bundle if needed, and SF4_PORT for hardware."

py-install: env
ifdef UV
	uv sync
else
	$(PYTHON) -m venv $(VENV)
	$(PIP) install -r requirements.txt
endif

web-install:
	$(NPM_INSTALL)

web-build: web-install
	npm --prefix web run build

install: py-install web-build
	@echo "install complete"

web-dev: web-install
	npm --prefix web run dev

# Build the UI on first run if it isn't there yet (generated/gitignored).
app/static/index.html:
	$(MAKE) web-build

run: py-install app/static/index.html
	$(UVICORN) app.main:app --host $(HOST) --port $(PORT)

dev: py-install app/static/index.html
	$(UVICORN) app.main:app --host $(HOST) --port $(PORT) --reload

cli: py-install
	$(SERIALCLI) $(ARGS)

port: py-install
	$(PY) -m app.devtools port

set-port: py-install env
	$(PY) -m app.devtools set-port

serial-perms:
	@test -n "$(SF4_PORT)" || { echo "set SF4_PORT in .env first, or run: make set-port"; exit 1; }
	sudo chmod a+rw $(SF4_PORT)
	@echo "temporary serial access granted for $(SF4_PORT)"

doctor: py-install
	@echo "Python: $$($(PY) --version)"
	@echo "Node:   $$(node --version 2>/dev/null || echo missing)"
	@echo "npm:    $$(npm --version 2>/dev/null || echo missing)"
	$(PY) -m app.devtools doctor

flash:
	@command -v arduino-cli >/dev/null || { echo "arduino-cli not installed"; exit 1; }
	arduino-cli compile --fqbn $(FQBN) $(SKETCH)
	@test -n "$(SF4_PORT)" || { echo "set SF4_PORT in .env to the board's port"; exit 1; }
	arduino-cli upload --fqbn $(FQBN) --port $(SF4_PORT) $(SKETCH)

clean:
	rm -rf $(VENV) app/__pycache__ __pycache__ app/static
	@echo "cleaned venv, caches, and built UI"
