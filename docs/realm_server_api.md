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

#### POST /shutdown

Initiates a server shutdown sequence.

**Response:** Empty response with a successful status code.

## Common Error Codes

- `MISSING_PARAMETER`: A required parameter is missing from the request
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