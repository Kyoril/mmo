# Realm Server HTTP API Documentation

## Overview

The Realm Server exposes an HTTP API that allows administrators to manage and monitor the server. This API provides endpoints for retrieving server status, managing Message of the Day (MOTD), creating world nodes, and controlling server operations such as shutdown.

## Authentication

All API endpoints require HTTP Basic Authentication. Use the realm server's configured password for authentication.

```
Authorization: Basic <base64 encoded credentials>
```

If authentication fails, the server will respond with a `401 Unauthorized` status code and prompt for credentials with the realm "MMO Realm".

## General Response Format

All API responses are returned as JSON with the appropriate HTTP status code.

### Success Responses

For successful operations, the server returns a `200 OK` status code with a JSON response body containing relevant data or a success message.

### Error Responses

For error conditions, the server returns an appropriate HTTP status code along with a JSON response body containing:

- `status`: An error code that identifies the type of error
- `message`: A human-readable description of the error

## Endpoints

### Server Status

#### GET /uptime

Returns the server uptime in seconds.

**Response:**
```json
{
  "uptime": 3600
}
```

### Message of the Day (MOTD)

#### GET /motd

Retrieves the current Message of the Day (MOTD).

**Response:**
```json
{
  "message": "Welcome to the server!"
}
```

#### POST /motd

Updates the Message of the Day (MOTD) that will be broadcasted to all players.

**Request Parameters:**
- `message` (required): The new Message of the Day text

**Response Status Codes:**
- `200 OK`: MOTD successfully updated
- `400 Bad Request`: Missing required parameter
- `500 Internal Server Error`: Failed to update MOTD

**Success Response Example:**
```json
{
  "status": "SUCCESS", 
  "message": "MOTD updated successfully"
}
```

**Error Response Example:**
```json
{
  "status": "MISSING_PARAMETER", 
  "message": "Missing parameter 'message'"
}
```

### Time of Day

The realm is the authority on the game's time of day. By default it is the realm's UTC system
time of day. A change is kept as an offset to the system clock — the clock keeps running from the
new value — until it is changed again or the realm restarts. Every change is pushed to all world
nodes, which retime their instances and tell every player, whose client blends the sky over to the
new time. The in-game GM console command `settime <HH:MM[:SS]|reset> [transition seconds]`
(GM level 1) does the same.

#### GET /time-of-day

**Response:**
```json
{
  "time": "21:30:12",
  "timeOfDayMs": 77412000,
  "systemTime": "14:02:45",
  "offsetMs": 26847000,
  "overridden": true
}
```

#### POST /time-of-day

Sets the time of day, or returns it to the system time.

**Request Parameters:**
- `time`: The new time of day as `HH:MM` or `HH:MM:SS` (24 hour clock). Required unless `reset` is given.
- `reset` (optional): `1` or `true` returns to the realm's system time; `time` is ignored.
- `transition` (optional): How many seconds clients take to blend over to the new time (default 8, capped at 60).

**Response Status Codes:**
- `200 OK`: Time of day changed; the body is the new state as returned by `GET /time-of-day`, plus `"status": "SUCCESS"`
- `400 Bad Request`: `MISSING_PARAMETER` or `INVALID_PARAMETER`

### World Management

#### POST /create-world

Creates a new world node with authentication credentials.

**Request Parameters:**
- `id` (required): The world node ID/name
- `password` (required): The password for the world node

**Response Status Codes:**
- `200 OK`: World successfully created
- `400 Bad Request`: Missing required parameters
- `409 Conflict`: World name already in use
- `500 Internal Server Error`: Failed to create world

**Error Response Example:**
```json
{
  "status": "WORLD_NAME_ALREADY_IN_USE", 
  "message": "World name already in use"
}
```

### Server Control

#### GET /shutdown

Reports whether a realm shutdown is pending.

**Response:**
```json
{
  "pending": true,
  "shuttingDown": false,
  "remaining": 540
}
```

`remaining` is the number of seconds until the shutdown (0 when none is pending). `shuttingDown` becomes true once the shutdown is due: the realm is then winding down (up to about 15 seconds) and will exit, so nothing is pending any more, but nothing can be scheduled or cancelled either.

#### POST /shutdown

Schedules a graceful realm shutdown. Scheduling again while one is pending replaces the pending shutdown.

**Parameters (form-encoded):**

| Name | Required | Description |
|---|---|---|
| `delay` | No | Whole seconds until the shutdown, from 0 up to 604800 (7 days). Defaults to 0 (shut down now). |

**Responses:**

| Status | Body |
|---|---|
| `200 OK` | `{ "status": "SUCCESS", "delay": 600 }` |
| `400 Bad Request` | `{ "status": "INVALID_PARAMETER", "message": "..." }` when `delay` is not a whole number or exceeds the maximum |
| `409 Conflict` | `{ "status": "SHUTTING_DOWN", "message": "The realm is already shutting down" }` once the shutdown is due |
| `503 Service Unavailable` | `{ "status": "UNAVAILABLE", "message": "..." }` if shutdown is not available |

#### POST /shutdown/cancel

Cancels the pending shutdown.

**Responses:**

| Status | Body |
|---|---|
| `200 OK` | `{ "status": "SUCCESS" }` |
| `409 Conflict` | `{ "status": "NOT_PENDING", "message": "No shutdown is pending" }` |
| `409 Conflict` | `{ "status": "SHUTTING_DOWN", "message": "The realm is already shutting down" }` once the shutdown is due |

A scheduled shutdown announces itself to players in chat, logs them out, stops every connected world node and then the realm; all exit with code 0. The realm leaves the login server's realm list the moment the shutdown becomes due, and it writes every character's data to the database before the process exits.

SIGTERM (`docker stop`) and Ctrl+C run the same sequence without the countdown, so characters are saved there too. A world node that receives SIGTERM on its own also saves its players before it exits. Give the containers enough time to finish: `compose.yml` sets `stop_grace_period: 5m`, because Docker otherwise kills them 10 s after the signal.

**Deployment note:** GM level 3 (operator) now grants realm shutdown rights, so existing accounts with `gm_level >= 3` should be audited before deploying this feature.

## Common Error Codes

- `MISSING_PARAMETER`: A required parameter is missing from the request
- `INVALID_PARAMETER`: A parameter has an invalid value
- `NOT_PENDING`: There is no pending shutdown to cancel
- `SHUTTING_DOWN`: The realm's shutdown is already due; it can no longer be scheduled or cancelled
- `UNAVAILABLE`: The requested feature is not available
- `WORLD_NAME_ALREADY_IN_USE`: The world name provided already exists
- `INTERNAL_SERVER_ERROR`: An unexpected server error occurred

## System Architecture Context

The Realm Server is part of the MMO's distributed architecture where:

1. **Login Server** handles account authentication
2. **Realm Server** manages character data, realm status, and acts as a proxy for world servers
3. **World Server** connects to the realm server to host actual gameplay and map instances

This API allows administrators to monitor and control the realm server, which serves as a central hub for player characters and world server communication within the MMO infrastructure.

## Implementation Notes

- All API endpoints validate required parameters and respond with appropriate error messages
- The MOTD update mechanism uses a signal/slot pattern to notify players when the message changes
- World node creation includes secure SRP6 authentication credential generation