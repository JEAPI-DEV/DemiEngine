# HTTP and REST APIs

Import `demi.network.http` to call HTTP/HTTPS services from gameplay. Requests
run outside the game loop. HTTP does not require an ENet session or a network
contract.

```lua
local Http = require("demi.network.http")
local Debug = require("demi.debug")

local Scores = {}

function Scores:on_start()
  local request, error = Http.get("https://example.com/api/scores", {
    headers = { Accept = "application/json" },
    timeout_ms = 10000,
  })
  if not request then
    Debug.log(error)
    return
  end
  self.request = request
end

function Scores:on_update(dt)
  if not self.request then return end
  local response = self.request:response()
  if not response then return end -- Still pending; no blocking wait.
  self.request = nil

  if response.error ~= "" then
    Debug.log("Request failed: " .. response.error_code)
  elseif not response.ok then
    Debug.log("Service returned HTTP " .. response.status)
  elseif response.json_error ~= "" then
    Debug.log(response.json_error)
  else
    self.scores = response.json
  end
end

function Scores:on_destroy()
  if self.request then self.request:cancel() end
end

return Scores
```

## Requests

`Http.get(url, options)` and `Http.post(url, options)` select the method.
`Http.request(options)` accepts a `url` and `method`, including PUT, PATCH,
DELETE and HEAD. All three return `request, nil`, or `nil, error` when the
request cannot be submitted. The options table is not changed.

Use `json` for a JSON body. It sets `Content-Type: application/json` unless
you supply a content type. Use `body` for a raw string, including binary data;
specifying both is an error.

```lua
local request, error = Http.post("https://example.com/api/scores", {
  headers = { Authorization = "Bearer " .. access_token },
  json = { score = 1200, checkpoints = { "start", "bridge" } },
})
```

Headers are string-to-string tables. Do not embed credentials in URLs or
commit access tokens into source. URLs must use HTTP or HTTPS. Redirects are
returned as 3xx responses rather than followed, so custom authorization headers
are not forwarded to another destination.

`timeout_ms` defaults to 30,000 ms and `connect_timeout_ms` to 5,000 ms.
Zero disables the overall timeout; a zero connection timeout uses libcurl's
default connection timeout.
`max_response_bytes` defaults to 16 MiB; increase it for a larger response or
set it to zero to remove the body limit. This is a per-request memory safeguard,
not an asset-size or project-size restriction.
`max_header_bytes` similarly defaults to 64 KiB, with zero disabling the limit.
The client respects libcurl's standard proxy environment settings.

## Responses and lifetime

`request:done()` checks completion. `request:response()` returns nil while
pending and the completed result afterward. Reading a response does not consume
it. Retain the handle until you have collected the result; its garbage
collection requests cancellation. Explicit `cancel()` is preferable when a
screen closes or its request becomes irrelevant. Stopping Play cancels the
runtime's requests and joins its HTTP worker.
Cancellation becomes visible to scripts immediately. Worker cleanup normally
finishes promptly, but the system-backed threaded DNS resolver can delay shutdown
while an OS lookup finishes; shutdown has no hard real-time deadline. The engine
does not abandon a worker that could still access destroyed state.

`ok` means successful transport with HTTP status 200–299. A 404 or 500 is a
completed HTTP response with its status/body, not a transport failure.
`error` and `error_code` describe connection, TLS, timeout, cancellation and
other transport failures. Response header names are lowercase and values are
arrays, retaining duplicate headers.

Completed response bytes are available as `body`. Transport failures discard
partial responses and report status zero. Content encodings supported by the
installed curl build, such as gzip, are
negotiated and decoded automatically. The body limit applies to decoded bytes;
headers retain the server's original values, including its encoded content
length. Unsupported or malformed encodings produce a transport error.

JSON decoding is separate: `json_error` reports invalid JSON without changing
the HTTP status or `ok`.
It also reports integers outside Lua's representable range; the raw body remains
available rather than returning a truncated value.
Decoded nulls use `require("demi.data").null`, and arrays retain their shape,
including empty arrays. `Data.kind` and `Data.is_null` can distinguish them.
Malformed/cyclic request values are rejected instead of silently dropping data.

## HTTPS and platforms

Certificate-chain and hostname verification are always enabled. There is no
insecure verification switch. For a private CA, pass its PEM trust bundle in
`ca_certificate_pem`; that changes the trusted roots, not the verification rules.
Never send credentials over plain HTTP.

Desktop uses libcurl's configured trust store. Android uses its current system
CA directory, preferring the updatable Conscrypt store when available. Android
app `network-security-config` and user-installed certificates are not implicitly
imported into this native trust store; pass the intended private CA explicitly.

Android projects with an explicit build configuration must declare
`android.permission.INTERNET`. Validation checks the HTTP import independently
of the optional ENet multiplayer backend. Browser builds are not qualified by
this native implementation.

The native transport uses [libcurl's multi interface](https://curl.se/libcurl/c/libcurl-multi.html)
on a worker, with Lua conversion on the gameplay thread. This replaces the old
blocking `Network.http_get`, `Network.http_post_form` and native `Network.lobby_*`
helpers. Lobby endpoint paths and payloads belong to gameplay code or a package.

Desktop builds require libcurl 7.88 or newer with TLS, asynchronous DNS and
thread-safe initialization. Android builds use pinned curl 8.22.0 with the
engine's mbedTLS dependency; no host curl library is copied into the APK.
