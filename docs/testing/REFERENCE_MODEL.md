# Reference Model

The slow, obviously correct order book (`ReferenceBook`) and its role as the differential-
testing oracle (INV-15).

## Design constraints (in priority order)

1. **Obviousness.** Every function should read like the corresponding EXCHANGE_RULES.md
   paragraph. A reviewer holding the rules doc must be able to verify each function by
   inspection in under a minute. Comments cite rule IDs.
2. **Independence.** Shares *no implementation code* with FastBook — only `core` types and
   the `OrderBookLike` concept. Different data structures on purpose (`std::map` +
   `std::list`): a shared bug requires the same mistake twice in different shapes.
3. **No performance consideration whatsoever.** Linear scans are fine; copying is fine.
   The moment someone optimizes the reference, it stops being one.

## Structure

```cpp
// conceptual shape, not final code
class ReferenceBook {
  std::map<Price, std::list<RefOrder>, std::greater<>> bids_;  // best first
  std::map<Price, std::list<RefOrder>, std::less<>>    asks_;
  std::map<OrderId, Locator> index_;      // Locator = side + price + list iterator
};
```

`RefOrder` duplicates fields rather than sharing the slab (independence). Level totals are
recomputed by summation on demand in checks (no cached aggregates to get wrong).

## Scope

The reference implements the *book and matching semantics* (add/reduce/remove/requeue/match
order selection). The gateway/sequencer/risk/accounting layers are shared with the fast path
— they are not performance-sensitive and having two of each would double the spec-drift
surface without adding oracle value. (Their correctness is carried by unit + property tests
directly.) The differential boundary is therefore exactly the `OrderBookLike` concept plus
the match loop, which the engine template instantiates with either book.

## Differential harness

```
for msg in scenario:
    fast_events = engine<FastBook>.process(msg)
    ref_events  = engine<ReferenceBook>.process(msg)
    assert fast_events == ref_events            # full event equality, not just book state
    assert dump(fast) == dump(ref)              # levels, order queues, aggregates
```

Event-stream equality is the stronger half: two books can end in the same state via different
(wrong) trades.

## Verification of the reference itself

The oracle needs its own grounding: (1) line-by-line review against EXCHANGE_RULES.md (a
Deep-review checkpoint in BUILD_SEQUENCE.md — the one component where human/strong-model
review is mandatory before anything is tested against it); (2) the scripted worked examples
(rules doc §15, accounting examples) assert exact expected outputs; (3) all unit and property
tests run against the reference first — it must pass everything the fast book must pass.

## Cost containment

Reference runs are O(N·book) per scenario; property CI budgets (PROPERTY_TESTS.md) are sized
for it. It is never benchmarked, never used in experiments (research runs use FastBook only,
after differential green), and compiled out of the Python extension entirely.
