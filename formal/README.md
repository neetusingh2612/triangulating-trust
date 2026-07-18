# Symbolic Verification (ProVerif)

```bash
make        # runs both models
```

Requires ProVerif >= 2.05.

## Models

**`tt_modelA_honest.pv`** — honest parties. Proves:
- secrecy of the extracted seed R
- secrecy of the group key
- (non-injective) agreement between sender and receiver

**Injective agreement is not proved.** ProVerif's Horn-clause abstraction
cannot establish one-pass counter freshness, and we state that limitation
rather than omitting the query. Tamarin, with explicit state, is the
appropriate tool if an injective result is required.

**`tt_modelB_capture.pv`** — ECU capture. Confirms:
- the group-key attribution gap (a captured member can produce frames
  attributable to any member of its domain) — this query is *expected* to be
  refuted, and its refutation is the point
- revocation restores the security properties

## Reproducing

Re-run on an official ProVerif build before relying on the output. The results
reported in the paper were obtained from a source build of 2.05.
