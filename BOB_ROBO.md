# Bob Robo — Hardware Concept

> A physical, always-on AI assistant — designed to sit on a desk, listen, think, and act.

---

## The Idea

Bob Robo is the idea of bringing Bob — the AI assistant — out of the screen and into the room. It is a small, standalone hardware device that lives on a developer's desk or in a team space. It wakes up when spoken to, processes the request through Bob's brain, and responds through voice, display, or action.

No keyboard. No browser tab. Just a physical presence that is always within reach — a teammate that never leaves the office.

---

## Idle Mode — Always Ready, Never Noisy

When nothing is happening, Bob Robo sits in **Idle Mode**. It is quiet, low-power, and unobtrusive — a soft ambient glow that signals it is awake but not demanding attention.

Idle Mode is not a dead state. Bob is still listening for its wake signal, still connected, and still ready to surface a notification if something important needs attention — a build failure, an incoming message, a scheduled reminder.

The transition from idle to active is instant. One word and Bob is fully engaged. One moment of silence and it drifts back, leaving the room calm.

---

## How `bob_robo_convo` Works

The conversation layer — `bob_robo_convo` — is the loop that keeps Bob and the user in sync during an active session. It manages the full lifecycle of an interaction from first word to final answer.

1. **Wake & Listen** — Device detects the wake signal and opens an audio stream. Ambient noise is filtered out.
2. **Transcribe** — Spoken words are converted to text and passed into the conversation context.
3. **Route** — Bob determines which agent, mode, or skill is best suited for the request — code, data, docs, or conversation.
4. **Process & Respond** — The selected agent handles the task and sends back a response — spoken aloud, shown on a display, or acted upon silently.
5. **Hold or Return to Idle** — If the conversation continues, the context is held open. When the exchange ends, `bob_robo_convo` closes cleanly and Bob returns to idle.

---

## Features That Could Be Added

- **Voice Persona Switching** — Change Bob's tone, verbosity, or domain focus mid-conversation — developer mode, meeting mode, or onboarding mode.
- **Ambient Notifications** — Bob lights up or chimes softly when something needs attention — no phone, no screen required.
- **Multi-User Awareness** — Recognise different voices in the room and maintain separate conversation contexts per person.
- **Local Offline Mode** — A lightweight fallback that handles common queries without a network connection.
- **Display Surface** — A small screen for showing code snippets, charts, or status — for moments when voice alone is not enough.
- **Action Triggers** — Bob executes a task — runs a pipeline, opens a ticket, sends a message — based on voice command alone.

---

## Scalable to Any Agent

The most important design principle of Bob Robo is that the hardware is just a surface. The intelligence lives in the agent layer — and that layer is open-ended. Bob's routing engine can hand off any request to any registered agent, meaning new capabilities are added by connecting agents, not by changing the device.

Today's set of agents is a starting point. The architecture is ready for many more:

| Agent | Role |
|---|---|
| Bob (Core) | General assistant, code, docs |
| Db2 Agent | Database queries and tuning |
| Code Review Agent | PR feedback and analysis |
| Pipeline Agent | Trigger and monitor CI/CD |
| Documentation Agent | Generate and search docs |
| Security Agent | Vulnerability scanning and advice |
| Incident Agent | Alert triage and runbooks |
| Meeting Summariser | Transcribe and summarise sessions |
| _+ any future agent_ | Plug in without touching the device |

Each agent is independently developed, independently versioned, and plugged into the same conversation loop. Bob Robo does not need to know what agents exist — it just needs to know how to ask.

---

## Vision

> "Bob Robo is not a smarter speaker. It is a physical seat at the table for an AI teammate — one that understands your codebase, knows your tools, and grows more capable every time a new agent is added. The device stays the same. The intelligence never stops expanding."
