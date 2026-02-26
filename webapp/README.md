# webapp

Local Pi web service for:
- mobile-first setup UI
- Wi-Fi onboarding flow
- config storage
- weather + flight ingestion
- `/api/state/current` for matrix-agent

## First endpoints to implement
- `GET /api/health`
- `GET /api/config`
- `POST /api/config`
- `GET /api/state/current`
- `POST /api/location` (from browser geolocation)

## Notes
- Bind to LAN interface.
- Persist config in SQLite.
- Keep API keys in environment variables.
