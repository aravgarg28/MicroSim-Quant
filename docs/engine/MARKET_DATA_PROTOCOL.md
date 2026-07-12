# Market-Data Protocol

The public feed: what the exchange tells the world. In-process message structs first (that is
all Releases 1–5 need); a serialized wire format is an explicitly optional Release-6 extension.
Governing rules: R-13 (information content), INV-9 (gap-free sequencing), INV-16 (consumer
book must equal engine book).

## Design choices

1. **Level-aggregated by default (price-level feed), order-by-order optional.** The public
   feed publishes per-level aggregate quantities (like most exchanges' standard depth feeds).
   An optional order-granular feed (add/replace/delete per anonymous order, MBO-style) exists
   from Release 4 because queue-position inference (RQ3) needs it — real queue estimation works
   exactly this way on venues that publish MBO. Both carry the same `md_seq` stream.
2. **No participant identities anywhere** (R-13.2). Trades carry aggressor side only.
3. **Consumers are message-pure:** the consumer library types depend only on `core` — the
   differential test "consumer book == engine book at every md_seq" (INV-16) proves the feed's
   sufficiency.

## Message types (in-process structs, all ≤ 64 bytes, trivially copyable)

Common header: `md_seq` (uint64, per-instrument, gap-free), `instrument_id`,
`ts_event` (engine logical time).

| Message | Fields beyond header | Emitted when |
|---|---|---|
| `LevelUpdate` | side, price_ticks, new_total_qty (0 ⇒ level removed), order_count | any resting-quantity change at a level |
| `TradeMD` | trade_id, price_ticks, qty_lots, aggressor_side | each trade (R-5.7) |
| `OrderAddMD` [R4, MBO feed] | anon_order_ref, side, price_ticks, qty_lots | order joins book |
| `OrderModifyMD` [R4] | anon_order_ref, new_qty (in-place reductions only) | priority-keeping modify |
| `OrderDeleteMD` [R4] | anon_order_ref | order leaves book (fill/cancel; reason not disclosed) |
| `SnapshotBegin/Level/End` | snapshot_id, consistent_md_seq; per-level rows | on request / at open |
| `SessionStatus` | state (OPEN/CLOSED) | transitions |

`anon_order_ref` is a per-session random-ish (but seeded/deterministic) alias of the order,
so consumers can track queues without learning exchange `order_id`s.

**Emission ordering within one inbound message** (fixed, testable): `TradeMD`s in execution
order, then `LevelUpdate`/MBO deltas in book-change order, exactly as MATCHING_ENGINE_SPEC.md
pseudocode lines execute. One inbound message may thus produce an atomic *batch* of MD
messages sharing `ts_event`, with consecutive `md_seq`s; a `batch_end` flag on the last
message lets consumers apply batches atomically (no torn views mid-sweep).

## Price/quantity representation

Identical to the engine (R-1.2): int64 ticks and lots. The feed never converts units.

## Timestamps

`ts_event` is engine processing time. Consumer-side delivery time is *not* in the message —
it is when the latency model delivers it; participants must timestamp receipt with their own
observed clock (which the simulation provides as "your current logical time"). This mirrors
reality: the feed tells you when the exchange acted, not when you found out.

## Gap detection and recovery

In-process delivery never drops, but the protocol still specifies recovery (and tests inject
faults) because gap handling is core exchange-connectivity competence:

- Consumer tracks `expected_md_seq`; on receiving `md_seq > expected`, it enters `GAPPED`
  state, stops updating, and requests a snapshot.
- Snapshot delivery: `SnapshotBegin(consistent_md_seq = K)` + levels + `SnapshotEnd`;
  consumer discards buffered deltas ≤ K, applies the snapshot, replays buffered deltas > K,
  returns to `LIVE`.
- Replay requests (full re-send from seq N) exist only in the replay engine, not the live
  protocol — a consumer that misses data recovers via snapshot, as on real feeds.

## Serialization (Release 6, optional)

If a wire format ships: fixed-width little-endian packed structs, version byte + message-type
byte, no varints, no allocation on decode — chosen because it is exactly what the in-process
structs already are (`memcpy`-able). Protobuf/JSON rejected for the data path (allocation,
speed, and the wrong lesson); JSON allowed for config/dashboard APIs only.

## Consumer API sketch (research-facing)

`ConsumerBook`: `apply(msg)`, `best(side)`, `depth(side, k_levels)`, `mid()`, `microprice()`,
`imbalance(k)`, `last_trade()`, `md_seq()`, `state()` (LIVE/GAPPED). Streaming features
(rolling volatility, trade-flow counters) layer on top in `metrics`/strategy code, not in the
consumer (single responsibility: the consumer reconstructs; it does not opine).
