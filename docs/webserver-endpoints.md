# Webserver Endpoints

This document describes all HTTP and WebSocket endpoints available on the CrossPoint Reader webserver.

- [Webserver Endpoints](#webserver-endpoints)
  - [Overview](#overview)
  - [HTTP Endpoints](#http-endpoints)
    - [GET `/` - Home Page](#get----home-page)
    - [GET `/files` - File Browser Page](#get-files---file-browser-page)
    - [GET `/api/status` - Device Status](#get-apistatus---device-status)
    - [GET `/api/plugins` - Compile-Time Feature Manifest](#get-apiplugins---compile-time-feature-manifest)
    - [GET `/api/files` - List Files](#get-apifiles---list-files)
    - [POST `/api/todo/entry` - Add TODO or Agenda Entry](#post-apitodoentry---add-todo-or-agenda-entry)
    - [GET `/download` - Download File](#get-download---download-file)
    - [POST `/upload` - Upload File](#post-upload---upload-file)
    - [POST `/api/user-fonts/rescan` - Rescan SD User Fonts](#post-apiuser-fontsrescan---rescan-sd-user-fonts)
    - [POST `/mkdir` - Create Folder](#post-mkdir---create-folder)
    - [POST `/delete` - Delete File or Folder](#post-delete---delete-file-or-folder)
  - [WebSocket Endpoint](#websocket-endpoint)
    - [Port 81 - Fast Binary Upload](#port-81---fast-binary-upload)
  - [Network Modes](#network-modes)
    - [Station Mode (STA)](#station-mode-sta)
    - [Access Point Mode (AP)](#access-point-mode-ap)
  - [Notes](#notes)


## Overview

The CrossPoint Reader exposes a webserver for file management and device monitoring:

- **HTTP Server**: Port 80
- **WebSocket Server**: Port 81 (for fast binary uploads)

---

## HTTP Endpoints

### GET `/` - Home Page

Serves the home page HTML interface.

**Request:**
```bash
curl http://crosspoint.local/
```

**Response:** HTML page (200 OK)

---

### GET `/files` - File Browser Page

Serves the file browser HTML interface.

**Request:**
```bash
curl http://crosspoint.local/files
```

**Response:** HTML page (200 OK)

---

### GET `/api/status` - Device Status

Returns JSON with device status information.

**Request:**
```bash
curl http://crosspoint.local/api/status
```

**Response (200 OK):**
```json
{
  "version": "1.0.0",
  "ip": "192.168.1.100",
  "mode": "STA",
  "rssi": -45,
  "freeHeap": 123456,
  "uptime": 3600
}
```

| Field      | Type   | Description                                               |
| ---------- | ------ | --------------------------------------------------------- |
| `version`  | string | CrossPoint firmware version                               |
| `ip`       | string | Device IP address                                         |
| `mode`     | string | `"STA"` (connected to WiFi) or `"AP"` (access point mode) |
| `rssi`     | number | WiFi signal strength in dBm (0 in AP mode)                |
| `freeHeap` | number | Free heap memory in bytes                                 |
| `uptime`   | number | Seconds since device boot                                 |

---

### GET `/api/plugins` - Compile-Time Feature Manifest

Returns JSON booleans describing which compile-time features are included in this firmware build.

**Request:**
```bash
curl http://crosspoint.local/api/plugins
```

**Response (200 OK, example):**
```json
{
  "markdown": true,
  "todo_planner": true,
  "web_pokedex_plugin": false
}
```

---

### GET `/api/files` - List Files

Returns a JSON array of files and folders in the specified directory.

**Request:**
```bash
# List root directory
curl http://crosspoint.local/api/files

# List specific directory
curl "http://crosspoint.local/api/files?path=/Books"
```

**Query Parameters:**

| Parameter | Required | Default | Description            |
| --------- | -------- | ------- | ---------------------- |
| `path`    | No       | `/`     | Directory path to list |

**Response (200 OK):**
```json
[
  {"name": "MyBook.epub", "size": 1234567, "isDirectory": false, "isEpub": true},
  {"name": "Notes", "size": 0, "isDirectory": true, "isEpub": false},
  {"name": "document.pdf", "size": 54321, "isDirectory": false, "isEpub": false}
]
```

| Field         | Type    | Description                              |
| ------------- | ------- | ---------------------------------------- |
| `name`        | string  | File or folder name                      |
| `size`        | number  | Size in bytes (0 for directories)        |
| `isDirectory` | boolean | `true` if the item is a folder           |
| `isEpub`      | boolean | `true` if the file has `.epub` extension |

**Notes:**
- Hidden files (starting with `.`) are automatically filtered out
- System folders (`System Volume Information`, `XTCache`) are hidden

---

### POST `/api/todo/entry` - Add TODO or Agenda Entry

Appends an entry to today's daily TODO file when the TODO planner feature is compiled in.

**Request:**
```bash
# Add a task entry
curl -X POST -d "type=todo&text=Buy milk" http://crosspoint.local/api/todo/entry

# Add an agenda/note entry
curl -X POST -d "type=agenda&text=Meeting at 14:00" http://crosspoint.local/api/todo/entry
```

**Form Parameters:**

| Parameter | Required | Description |
| --------- | -------- | ----------- |
| `text`    | Yes      | Entry text (1-300 chars, newlines are normalized to spaces) |
| `type`    | No       | `todo` (default) or `agenda` |

**Storage selection:**
- If today's `.md` file exists, append there.
- Else if today's `.txt` file exists, append there.
- Else create `.md` when markdown support is enabled, or `.txt` when markdown support is disabled.

**Response (200 OK):**
```json
{"ok":true}
```

**Error Responses:**

| Status | Body | Cause |
| ------ | ---- | ----- |
| 400 | `Missing text` | `text` parameter missing |
| 400 | `Invalid text` | Empty or too long text |
| 404 | `TODO planner disabled` | Feature is not compiled in |
| 503 | `Date unavailable` | Device date could not be resolved |
| 500 | `Failed to write TODO entry` | SD write failed |

---

### GET `/download` - Download File

Downloads a file from the SD card.

**Request:**
```bash
# Download from root
curl -L "http://crosspoint.local/download?path=/mybook.epub" -o mybook.epub

# Download from a nested folder
curl -L "http://crosspoint.local/download?path=/Books/Fiction/mybook.epub" -o mybook.epub
```

**Query Parameters:**

| Parameter | Required | Description              |
| --------- | -------- | ------------------------ |
| `path`    | Yes      | Absolute file path on SD |

**Response (200 OK):**
- Binary file stream (`application/octet-stream` for most files)
- `application/epub+zip` for `.epub` files
- `Content-Disposition: attachment; filename="..."`

**Error Responses:**

| Status | Body                                | Cause                           |
| ------ | ----------------------------------- | ------------------------------- |
| 400    | `Missing path`                      | `path` parameter not provided   |
| 400    | `Invalid path`                      | Invalid or root path            |
| 400    | `Path is a directory`               | Attempted to download a folder  |
| 403    | `Cannot access system files`        | Hidden file (starts with `.`)   |
| 403    | `Cannot access protected items`     | Protected system file/folder    |
| 404    | `Item not found`                    | Path does not exist             |
| 500    | `Failed to open file`               | SD card access/open error       |

**Protected Items:**
- Files/folders starting with `.`
- `System Volume Information`
- `XTCache`

---

### POST `/upload` - Upload File

Uploads a file to the SD card via multipart form data.

**Request:**
```bash
# Upload to root directory
curl -X POST -F "file=@mybook.epub" http://crosspoint.local/upload

# Upload to specific directory
curl -X POST -F "file=@mybook.epub" "http://crosspoint.local/upload?path=/Books"
```

**Query Parameters:**

| Parameter | Required | Default | Description                     |
| --------- | -------- | ------- | ------------------------------- |
| `path`    | No       | `/`     | Target directory for the upload |

**Response (200 OK):**
```
File uploaded successfully: mybook.epub
```

**Error Responses:**

| Status | Body                                            | Cause                       |
| ------ | ----------------------------------------------- | --------------------------- |
| 400    | `Failed to create file on SD card`              | Cannot create file          |
| 400    | `Failed to write to SD card - disk may be full` | Write error during upload   |
| 400    | `Failed to write final data to SD card`         | Error flushing final buffer |
| 400    | `Upload aborted`                                | Client aborted the upload   |
| 400    | `Unknown error during upload`                   | Unspecified error           |

**Notes:**
- Existing files with the same name will be overwritten
- Uses a 4KB buffer for efficient SD card writes

---

### POST `/api/user-fonts/rescan` - Rescan SD User Fonts

Rescans the `/fonts` directory for `.cpf` fonts and reloads the currently selected external font if enabled.

**Request:**
```bash
curl -X POST http://crosspoint.local/api/user-fonts/rescan
```

**Response (200 OK):**
```json
{
  "families": 3,
  "activeLoaded": true
}
```

| Field          | Type    | Description |
| -------------- | ------- | ----------- |
| `families`     | number  | Number of discovered font families |
| `activeLoaded` | boolean | `true` when the active external font could be loaded after rescan |

---

### POST `/mkdir` - Create Folder

Creates a new folder on the SD card.

**Request:**
```bash
curl -X POST -d "name=NewFolder&path=/" http://crosspoint.local/mkdir
```

**Form Parameters:**

| Parameter | Required | Default | Description                  |
| --------- | -------- | ------- | ---------------------------- |
| `name`    | Yes      | -       | Name of the folder to create |
| `path`    | No       | `/`     | Parent directory path        |

**Response (200 OK):**
```
Folder created: NewFolder
```

**Error Responses:**

| Status | Body                          | Cause                         |
| ------ | ----------------------------- | ----------------------------- |
| 400    | `Missing folder name`         | `name` parameter not provided |
| 400    | `Folder name cannot be empty` | Empty folder name             |
| 400    | `Folder already exists`       | Folder with same name exists  |
| 500    | `Failed to create folder`     | SD card error                 |

---

### POST `/delete` - Delete File or Folder

Deletes a file or folder from the SD card.

**Request:**
```bash
# Delete a file
curl -X POST -d "path=/Books/mybook.epub&type=file" http://crosspoint.local/delete

# Delete an empty folder
curl -X POST -d "path=/OldFolder&type=folder" http://crosspoint.local/delete
```

**Form Parameters:**

| Parameter | Required | Default | Description                      |
| --------- | -------- | ------- | -------------------------------- |
| `path`    | Yes      | -       | Path to the item to delete       |
| `type`    | No       | `file`  | Type of item: `file` or `folder` |

**Response (200 OK):**
```
Deleted successfully
```

**Error Responses:**

| Status | Body                                          | Cause                         |
| ------ | --------------------------------------------- | ----------------------------- |
| 400    | `Missing path`                                | `path` parameter not provided |
| 400    | `Cannot delete root directory`                | Attempted to delete `/`       |
| 400    | `Folder is not empty. Delete contents first.` | Non-empty folder              |
| 403    | `Cannot delete system files`                  | Hidden file (starts with `.`) |
| 403    | `Cannot delete protected items`               | Protected system folder       |
| 404    | `Item not found`                              | Path does not exist           |
| 500    | `Failed to delete item`                       | SD card error                 |

**Protected Items:**
- Files/folders starting with `.`
- `System Volume Information`
- `XTCache`

---

## WebSocket Endpoint

### Port 81 - Fast Binary Upload

A WebSocket endpoint for high-speed binary file uploads. More efficient than HTTP multipart for large files.

**Connection:**
```
ws://crosspoint.local:81/
```

**Protocol:**

1. **Client** sends TEXT message: `START:<filename>:<size>:<path>`
2. **Server** responds with TEXT: `READY`
3. **Client** sends BINARY messages with file data chunks
4. **Server** sends TEXT progress updates: `PROGRESS:<received>:<total>`
5. **Server** sends TEXT when complete: `DONE` or `ERROR:<message>`

**Example Session:**

```
Client -> "START:mybook.epub:1234567:/Books"
Server -> "READY"
Client -> [binary chunk 1]
Client -> [binary chunk 2]
Server -> "PROGRESS:65536:1234567"
Client -> [binary chunk 3]
...
Server -> "PROGRESS:1234567:1234567"
Server -> "DONE"
```

**Error Messages:**

| Message                           | Cause                              |
| --------------------------------- | ---------------------------------- |
| `ERROR:Failed to create file`     | Cannot create file on SD card      |
| `ERROR:Invalid START format`      | Malformed START message            |
| `ERROR:No upload in progress`     | Binary data received without START |
| `ERROR:Write failed - disk full?` | SD card write error                |

**Example with `websocat`:**
```bash
# Interactive session
websocat ws://crosspoint.local:81

# Then type:
START:mybook.epub:1234567:/Books
# Wait for READY, then send binary data
```

**Notes:**
- Progress updates are sent every 64KB or at completion
- Disconnection during upload will delete the incomplete file
- Existing files with the same name will be overwritten

---

## Network Modes

The device can operate in two network modes:

### Station Mode (STA)
- Device connects to an existing WiFi network
- IP address assigned by router/DHCP
- `mode` field in `/api/status` returns `"STA"`
- `rssi` field shows signal strength

### Access Point Mode (AP)
- Device creates its own WiFi hotspot
- Default IP is typically `192.168.4.1`
- `mode` field in `/api/status` returns `"AP"`
- `rssi` field returns `0`

---

## Notes

- These examples use `crosspoint.local`. If your network does not support mDNS or the address does not resolve, replace it with the specific **IP Address** displayed on your device screen (e.g., `http://192.168.1.102/`).
- All paths on the SD card start with `/`
- Trailing slashes are automatically stripped (except for root `/`)
- The webserver uses chunked transfer encoding for file listings
