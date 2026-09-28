# Background tasks

Run `demi run --project examples/async_tasks` from the repository root.

An ongoing calculation starts automatically on a pooled Lua worker. Move
the marker with WASD/arrows and press Space or the Urgent action button while it
runs. Start/cancel toggles the loop. SQLite query runs a parameterized in-memory
query from a second Lua worker. It imports `demi.database` inside the worker,
opens a ready connection with `Database.open`, then uses `query(...):wait()`
and `close():wait()` in sequence. Native database work runs on its own pool;
the Lua worker waits for completion notifications. The main update polls only
the task handle and displays its copied row or error in the HUD.

Run `demi test linux --project examples/async_tasks` for the visible HUD/SQLite
end-to-end check.

The worker publishes progress through an explicitly shared native map. Its
cancellation check runs between batches. Ordinary input tables and returned
values are copied between VMs; game entities remain on the main thread.
Database connections and operations stay inside the worker. The connection is
closed before returning the row, including when the query reports an error.
Scene cleanup requests cancellation of both Lua tasks.

See the website documentation's **Background tasks**, **Databases & drivers**,
and **TCP client** pages. Ordinary input, fixed updates, and action handlers
remain on the main thread; no instruction hooks are added to them.
