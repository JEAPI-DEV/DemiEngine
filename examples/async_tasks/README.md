# Background tasks

Run `demi run --project examples/async_tasks` from the repository root.

An ongoing calculation starts automatically on a pooled Lua worker. Move
the marker with WASD/arrows and press Space or the Urgent action button while it
runs. Start/cancel toggles the loop. SQLite query runs a parameterized in-memory
query on the native database pool; the main callback polls its operation handle.

Run `demi test linux --project examples/async_tasks` for the visible HUD/SQLite
end-to-end check.

The worker publishes progress through an explicitly shared native map. Its
cancellation check runs between batches. Ordinary input tables and returned
values are copied between VMs; game entities remain on the main thread.

See the website documentation's **Background tasks**, **Databases & drivers**,
and **TCP client** pages. Ordinary input, fixed updates, and action handlers
remain on the main thread; no instruction hooks are added to them.
