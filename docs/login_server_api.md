# Login Server HTTP API Documentation

## Overview

The Login Server exposes an HTTP API that allows administrators to manage accounts, realms, and monitor the server. This API provides endpoints for creating accounts, managing realms, handling account bans, and controlling server operations such as shutdown.

## Authentication

All API endpoints require HTTP Basic Authentication. Use the login server's configured password for authentication.

```
Authorization: Basic <base64 encoded credentials>
```

If authentication fails, the server will respond with a `401 Unauthorized` status code and prompt for credentials with the realm "MMO Login".

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

### Account Management

#### POST /create-account

Creates a new user account with authentication credentials.

**Request Parameters:**
- `id` (required): The account ID/name
- `password` (required): The password for the account

**Response Status Codes:**
- `200 OK`: Account successfully created
- `400 Bad Request`: Missing required parameters
- `409 Conflict`: Account name already in use
- `500 Internal Server Error`: Failed to create account

**Error Response Example:**
```json
{
  "status": "ACCOUNT_NAME_ALREADY_IN_USE", 
  "message": "Account name already in use"
}
```

### Realm Management

#### POST /create-realm

Creates a new realm with authentication credentials and connection information.

**Request Parameters:**
- `id` (required): The realm ID/name
- `password` (required): The password for the realm
- `address` (required): The IP address or hostname for the realm server
- `port` (required): The port number for the realm server

**Response Status Codes:**
- `200 OK`: Realm successfully created
- `400 Bad Request`: Missing required parameters
- `409 Conflict`: Realm name already in use
- `500 Internal Server Error`: Failed to create realm

**Error Response Example:**
```json
{
  "status": "ACCOUNT_NAME_ALREADY_IN_USE", 
  "message": "Realm name already in use"
}
```

### Ban Management

#### POST /ban-account

Bans an existing account, preventing the user from logging in.

**Request Parameters:**
- `account_name` (required): The account name to ban
- `expiration` (optional): The ban expiration datetime in format "YYYY-MM-DD HH:MM:SS" (permanent ban if omitted)
- `reason` (optional): The reason for the ban (max 256 characters)

**Response Status Codes:**
- `200 OK`: Account successfully banned
- `400 Bad Request`: Missing required parameters or invalid format
- `404 Not Found`: Account does not exist
- `409 Conflict`: Account is already banned
- `500 Internal Server Error`: Failed to ban account

**Success Response Example:**
```json
{
  "status": "SUCCESS"
}
```

**Error Response Example:**
```json
{
  "status": "ACCOUNT_DOES_NOT_EXIST",
  "message": "An account with the name 'example' does not exist!"
}
```

#### POST /unban-account

Removes a ban from an existing account.

**Request Parameters:**
- `account_name` (required): The account name to unban
- `reason` (optional): The reason for removing the ban (max 256 characters)

**Response Status Codes:**
- `200 OK`: Account successfully unbanned
- `400 Bad Request`: Missing required parameters
- `500 Internal Server Error`: Failed to unban account

**Success Response Example:**
```json
{
  "status": "SUCCESS"
}
```

### GM Level Management

#### GET /gm-level

Retrieves the GM level for an account.

**Request Parameters:**
- `account_name` (required): The account name to check

**Response Status Codes:**
- `200 OK`: Successfully retrieved GM level
- `400 Bad Request`: Missing required parameters
- `404 Not Found`: Account does not exist
- `500 Internal Server Error`: Failed to retrieve GM level

**Success Response Example:**
```json
{
  "status": "SUCCESS",
  "account_name": "admin",
  "gm_level": 3
}
```

**Error Response Example:**
```json
{
  "status": "ACCOUNT_DOES_NOT_EXIST",
  "message": "An account with the name 'unknown' does not exist!"
}
```

#### POST /gm-level

Sets the GM level for an account. If the account is currently logged in, the player will be kicked to apply the new GM level.

**Request Parameters:**
- `account_name` (required): The account name to modify
- `gm_level` (required): The new GM level (integer between 0 and 255)

**Response Status Codes:**
- `200 OK`: GM level successfully updated
- `400 Bad Request`: Missing required parameters or invalid GM level
- `404 Not Found`: Account does not exist
- `500 Internal Server Error`: Failed to update GM level

**Success Response Example:**
```json
{
  "status": "SUCCESS",
  "account_name": "admin",
  "gm_level": 3
}
```

**Error Response Example:**
```json
{
  "status": "INVALID_PARAMETER",
  "message": "Parameter 'gm_level' must be between 0 and 255"
}
```

### Server Control

#### POST /shutdown

Initiates a server shutdown sequence.

**Response:** Empty response with a successful status code.

## Common Error Codes

- `MISSING_PARAMETER`: A required parameter is missing from the request
- `INVALID_PARAMETER`: A parameter has an invalid format or exceeds allowed limits
- `ACCOUNT_NAME_ALREADY_IN_USE`: The account name provided already exists
- `ACCOUNT_DOES_NOT_EXIST`: The specified account does not exist
- `ACCOUNT_ALREADY_BANNED`: The specified account is already banned
- `INTERNAL_SERVER_ERROR`: An unexpected server error occurred

## System Architecture Context

The Login Server is part of the MMO's distributed architecture where:

1. **Login Server** handles account authentication and management
2. **Realm Server** manages character data, realm status, and acts as a proxy for world servers
3. **World Server** connects to the realm server to host actual gameplay and map instances

This API allows administrators to manage user accounts, control realm registration, and monitor the login server which serves as the entry point for players into the MMO infrastructure.

## Implementation Notes

- All API endpoints validate required parameters and respond with appropriate error messages
- Account creation and realm registration use secure SRP6 authentication credential generation
- Ban management includes temporary bans with expiration dates and permanent bans