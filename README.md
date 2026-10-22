# RDR2 Voice AI

An experimental Red Dead Redemption 2 mod project exploring AI-powered conversations with NPCs.

## Status

**Early-stage project / scaffold.** This repository is not yet a ready-to-install mod. Do not expect a working DLL until the game integration, dependencies, and runtime behavior have been implemented and tested.

## Intended goals

- Start and stop an NPC conversation through a configurable player key.
- Send relevant conversation context to an AI provider.
- Keep API keys in a local configuration file or environment variable, never committed to Git.
- Handle timeouts, provider errors, and rate limits without interrupting gameplay.
- Keep dialogue appropriate to the NPC and the game setting.
- Provide clear setup, troubleshooting, and privacy documentation.

## Development plan

1. Inspect and document the installed RDR2 scripting/runtime requirements.
2. Create a minimal script that loads safely in-game.
3. Add configurable interaction input and NPC context.
4. Add an optional AI provider adapter and local configuration.
5. Add logging, error handling, and performance safeguards.
6. Test in-game and document known limitations before publishing releases.

## Security

- Never commit API keys, access tokens, passwords, or private configuration files.
- If a key was accidentally committed, revoke it and create a new one.
- AI requests may send dialogue/context to the provider you configure. Review that provider's privacy terms before use.

## Contributions

Issues and pull requests are welcome. Include the RDR2 version, scripting runtime version, relevant log output, and reproduction steps when reporting a problem.

## Disclaimer

This is an independent community project and is not affiliated with Rockstar Games. Red Dead Redemption 2 is a trademark of its respective owner.
